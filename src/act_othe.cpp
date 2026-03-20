/**************************************************************************
 *   File: act.other.c                                   Part of CircleMUD *
 *  Usage: Miscellaneous player-level commands                             *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#include "platdef.h"
#include <algorithm>
#include <array>
#include <ctype.h>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "char_utils.h"
#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "profs.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

/* extern variables */
extern struct descriptor_data* descriptor_list;
extern struct char_data* waiting_list;
extern struct index_data* mob_index;
extern struct skill_data skills[];
extern struct room_data world;
extern byte language_skills[];
extern byte language_number;
extern char* prof_abbrevs[];
extern char* race_abbrevs[];
extern int top_of_world;
extern int rev_dir[];
extern char* dirs[];

/* extern procedures */
extern int old_search_block(char*, int, unsigned int, const char**, int);
extern int get_followers_level(struct char_data*);
extern int get_real_stealth(struct char_data*);
extern void check_break_prep(struct char_data*);
SPECIAL(shop_keeper);
ACMD(do_gen_com);
ACMD(do_look);
ACMD(do_gsay);
ACMD(do_say);
ACMD(do_hit);

ACMD(do_quit)
{
    int tmp;
    affected_type* aff = affected_by_spell(ch, SPELL_FAME_WAR);
    void die(struct char_data*, struct char_data*, int);

    if (IS_NPC(ch) || !ch->desc || !ch->desc->descriptor)
        return;

    if (subcmd != SCMD_QUIT) {
        send_to_char("You have to type quit - no less - to quit!\n\r", ch);
        return;
    }

    if (GET_POS(ch) == POSITION_FIGHTING) {
        send_to_char("No way!  You're fighting for your life!\n\r", ch);
        return;
    }

    if (affected_by_spell(ch, SPELL_ANGER) && GET_LEVEL(ch) < LEVEL_IMMORT) {
        send_to_char("You may not quit yet.\n\r", ch);
        return;
    }

    if (GET_POS(ch) < POSITION_STUNNED) {
        send_to_char("You die before your time...\n\r", ch);
        die(ch, 0, 0);
        return;
    }

    if (GET_LEVEL(ch) >= LEVEL_IMMORT)
        act("Goodbye, friend.. Come back soon!", FALSE, ch, 0, 0, TO_CHAR);
    else
        act("As you quit, all your posessions drop to the ground!", FALSE, ch, 0, 0, TO_CHAR);

    act("$n has left the game.", TRUE, ch, 0, 0, TO_ROOM);

    sprintf(buf, "%s has quit the game.", GET_NAME(ch));

    mudlog(buf, NRM, (sh_int)MAX(LEVEL_GOD, GET_INVIS_LEV(ch)), TRUE);

    if (aff) {
        remove_fame_war_bonuses(ch, aff);
        affect_remove(ch, aff);
    }

    if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
        Crash_crashsave(ch, RENT_QUIT);
        for (tmp = 0; tmp < MAX_WEAR; tmp++)
            if (ch->equipment[tmp])
                extract_obj(unequip_char(ch, tmp));
        while (ch->carrying)
            extract_obj(ch->carrying);
        /* char is saved in extract_char */
        extract_char(ch);
    } else {
        /* same as above */
        extract_char(ch);
        Crash_crashsave(ch);
    }
}

ACMD(do_save)
{
    if (IS_NPC(ch) || !ch->desc)
        return;

    if (cmd) {
        sprintf(buf, "Saving %s.\n\r", GET_NAME(ch));
        send_to_char(buf, ch);
    }
    save_char(ch, NOWHERE, 0);
    Crash_crashsave(ch);
}

ACMD(do_not_here) { send_to_char("Sorry, but you can't do that here!\n\r", ch); }

ACMD(do_recruit)
{
    struct char_data* victim;
    struct affected_type af;
    struct follow_type *j, *k;
    int level_total = 0, num_followers = 0;

    one_argument(argument, arg);

    if (GET_RACE(ch) != RACE_ORC) {
        send_to_char("You can't recruit.\n\r", ch);
        return;
    }

    if (IS_NPC(ch) && IS_AFFECTED(ch, AFF_CHARM)) {
        send_to_char("Your superior would not approve of your building an army.\n\r", ch);
        return;
    }

    if (IS_SHADOW(ch)) {
        send_to_char("Who'd take orders from a shadow?\n\r", ch);
        return;
    }

    if (*arg) {
        if (!str_cmp(arg, "all")) {
            send_to_char("Recruiting an army cannot be rushed, "
                         "choose one follower at a time.\r\n",
                ch);
            return;
        }
    }

    for (k = ch->followers; k; k = j) {
        j = k->next;
        if (IS_NPC(k->follower) && MOB_FLAGGED(k->follower, MOB_ORC_FRIEND)) {
            level_total += GET_LEVEL(k->follower);
            num_followers++;
        }
    }

    if (!(victim = get_char_room_vis(ch, arg)) || !CAN_SEE(ch, victim)) {
        send_to_char("Recruit who?\n\r", ch);
        return;
    }

    if (!IS_NPC(victim) || !(MOB_FLAGGED(victim, MOB_ORC_FRIEND))) {
        char buf[1024];

        sprintf(buf, "%s doesn't want to be recruited.\n\r", GET_NAME(victim));
        send_to_char(buf, ch);
        return;
    }

    if (GET_LEVEL(victim) > ((GET_LEVEL(ch) * 2 + 2) / 5)) {
        send_to_char("That target is too high a level to recruit.\n\r", ch);
        return;
    }

    /*
     * Orcs get four followers with a cap of 48 total levels;
     * one follower level per one player level until level 48.
     * We allow orcs such a large force due to the fact that
     * they can't follow or be followed by other players.
     */
    if (level_total + GET_LEVEL(victim) > MIN(GET_LEVEL(ch), 48) || num_followers >= 4) {
        send_to_char("You have already recruited too powerful "
                     "a force for this target to be added.\n\r",
            ch);
        return;
    }

    /*- change 'your target' to the appropriate message (animal's name) */
    if (GET_POS(victim) == POSITION_FIGHTING) {
        send_to_char("Your target is too enraged!\n\r", ch);
        return;
    }

    if (IS_AFFECTED(victim, AFF_CHARM)) {
        send_to_char("Your target has already been charmed.\n\r", ch);
        return;
    }

    if (victim->master) {
        send_to_char("Your target is already following someone else.\n\r", ch);
        return;
    }

    if (victim == ch) {
        send_to_char("Aren't we funny today...\n\r", ch);
        return;
    }

    if (GET_POS(ch) == POSITION_FIGHTING) {
        send_to_char("You are too busy to do that.\n\r", ch);
        return;
    }

    if (circle_follow(victim, ch, FOLLOW_MOVE)) {
        send_to_char("Sorry, following in circles is not allowed.\n\r", ch);
        return;
    }

    if (victim->master)
        stop_follower(victim, FOLLOW_MOVE);
    affect_from_char(victim, SKILL_RECRUIT);
    add_follower(victim, ch, FOLLOW_MOVE);

    act("$n joins the company of $N.", FALSE, victim, 0, ch, TO_ROOM);
    af.type = SKILL_RECRUIT;
    af.duration = -1;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = AFF_CHARM;

    affect_to_char(victim, &af);

    REMOVE_BIT(MOB_FLAGS(victim), MOB_SPEC);
    victim->specials.store_prog_number = 0;
    REMOVE_BIT(MOB_FLAGS(victim), MOB_AGGRESSIVE);
    REMOVE_BIT(MOB_FLAGS(victim), MOB_STAY_ZONE);
    SET_BIT(MOB_FLAGS(victim), MOB_PET);

    victim->specials2.pref = 0; /* remove mob aggressions */
}

ACMD(do_title)
{
    for (; isspace(*argument); argument++)
        ;

    if (IS_NPC(ch))
        send_to_char("Your title is fine... go away.\n\r", ch);
    else if (GET_LEVEL(ch) < 20)
        send_to_char("You can't set your title yet.\n\r", ch);
    else if (PLR_FLAGGED(ch, PLR_NOTITLE))
        send_to_char("You can't title yourself - you shouldn't have abused it!\n\r", ch);
    else if (!*argument) {
        sprintf(buf, "Your present title is: %s\n\r", GET_TITLE(ch));
        send_to_char(buf, ch);
    } else if (strstr(argument, "(") || strstr(argument, ")"))
        send_to_char("Titles can't contain the ( or ) characters.\n\r", ch);
    else if (strlen(argument) > 80)
        send_to_char("Sorry, titles can't be longer than 80 characters.\n\r", ch);
    else {
        if (GET_TITLE(ch))
            RELEASE(GET_TITLE(ch));
        CREATE(GET_TITLE(ch), char, strlen(argument) + 1);
        strcpy(GET_TITLE(ch), argument);

        sprintf(buf, "OK, you're now %s %s.\n\r", GET_NAME(ch), GET_TITLE(ch));
        send_to_char(buf, ch);
    }
}

void roll_for_character(char_data* character, char_data* roll_initiator)
{
    if (utils::is_pc(*character)) {
        int roll = number(1, 100);
        sprintf(buf, "%8s -- Rolled: %3d", utils::get_name(*character), roll);
        act(buf, FALSE, roll_initiator, 0, 0, TO_CHAR);
        act(buf, FALSE, roll_initiator, 0, 0, TO_ROOM);
    }
}

bool compareByValue(const group_roll& a, const group_roll b) { return a.roll > b.roll; };

ACMD(do_grouproll)
{
    one_argument(argument, buf);

    if (IS_SHADOW(ch)) {
        send_to_char("You cannot roll for groups while inhabiting the shadow world.\n\r", ch);
        return;
    }

    if (ch->group == NULL) {
        send_to_char("But you are not in a group!\n\r", ch);
        return;
    }

    if (ch->group->get_leader() != ch) {
        send_to_char("Only the group leader can roll for the group.\n\r", ch);
        return;
    }

    if (string_func::is_null_or_empty(buf)) {
        std::vector<group_roll> group_rolls;

        for (auto& iter : *ch->group) {
            if (utils::is_pc(*iter)) {
                group_rolls.emplace_back(iter, number(1, 100));
            }
        }

        std::sort(group_rolls.begin(), group_rolls.end(), compareByValue);

        for (const auto& group_roll : group_rolls) {
            sprintf(buf, "%8s -- Rolled: %3d", group_roll.character_name, group_roll.roll);
            act(buf, FALSE, ch, nullptr, nullptr, TO_CHAR);
            act(buf, FALSE, ch, nullptr, nullptr, TO_ROOM);
        }

    } else {
        if (char_data* rollee = get_char_room_vis(ch, buf)) {
            if (ch->group == rollee->group) {
                roll_for_character(rollee, ch);
            } else {
                send_to_char("That person is not in the group!\n\r", ch);
            }
        } else {
            send_to_char("There is no such person!\n\r", ch);
        }
    }
}

void print_group_leader(const char_data* leader)
{
    extern struct prompt_type prompt_hit[];
    extern struct prompt_type prompt_mana[];
    extern struct prompt_type prompt_move[];

    int health_prompt_index = 0;
    int mana_prompt_index = 0;
    int move_prompt_index = 0;

    // For loop just gets prompt indices to the correct spot.
    for (health_prompt_index = 0;
         (1000 * GET_HIT(leader)) / GET_MAX_HIT(leader) > prompt_hit[health_prompt_index].value;
         health_prompt_index++)
        ;

    for (mana_prompt_index = 0;
         (1000 * GET_MANA(leader)) / GET_MAX_MANA(leader) > prompt_mana[mana_prompt_index].value;
         mana_prompt_index++)
        ;

    for (move_prompt_index = 0;
         (1000 * GET_MOVE(leader)) / GET_MAX_MOVE(leader) > prompt_move[move_prompt_index].value;
         move_prompt_index++)
        ;

    sprintf(
        buf, "HP:%9s,%11s,%13s -- %s (Head of group)\n\r", prompt_hit[health_prompt_index].message,
        *prompt_mana[mana_prompt_index].message == '\0' ? "S:Full"
                                                        : prompt_mana[mana_prompt_index].message,
        *prompt_move[move_prompt_index].message == '\0' ? "MV:Energetic"
                                                        : prompt_move[move_prompt_index].message,
        GET_NAME(leader));
}

void print_group_member(const char_data* group_member)
{
    extern struct prompt_type prompt_hit[];
    extern struct prompt_type prompt_mana[];
    extern struct prompt_type prompt_move[];

    int health_prompt_index = 0;
    int mana_prompt_index = 0;
    int move_prompt_index = 0;

    // For loop just gets prompt indices to the correct spot.
    for (health_prompt_index = 0;
         (1000 * GET_HIT(group_member)) / (GET_MAX_HIT(group_member) == 0 ? 1 : GET_MAX_HIT(group_member)) > prompt_hit[health_prompt_index].value;
         health_prompt_index++)
        ;

    for (mana_prompt_index = 0;
         (1000 * GET_MANA(group_member)) / (GET_MAX_MANA(group_member) == 0 ? 1 : GET_MAX_MANA(group_member)) > prompt_mana[mana_prompt_index].value;
         mana_prompt_index++)
        ;

    for (move_prompt_index = 0;
         (1000 * GET_MOVE(group_member)) / (GET_MAX_MOVE(group_member) == 0 ? 1 : GET_MAX_MOVE(group_member)) > prompt_move[move_prompt_index].value;
         move_prompt_index++)
        ;

    if (MOB_FLAGGED(group_member, MOB_ORC_FRIEND)) {
        sprintf(buf, "HP:%9s,%11s,%13s -- %s (Lvl:%2d)\n\r",
            prompt_hit[health_prompt_index].message,
            *prompt_mana[mana_prompt_index].message == '\0'
                ? "S:Full"
                : prompt_mana[mana_prompt_index].message,
            *prompt_move[move_prompt_index].message == '\0'
                ? "MV:Energetic"
                : prompt_move[move_prompt_index].message,
            GET_NAME(group_member), GET_LEVEL(group_member));
    } else {
        sprintf(buf, "HP:%9s,%11s,%13s -- %s\n\r", prompt_hit[health_prompt_index].message,
            *prompt_mana[mana_prompt_index].message == '\0'
                ? "S:Full"
                : prompt_mana[mana_prompt_index].message,
            *prompt_move[move_prompt_index].message == '\0'
                ? "MV:Energetic"
                : prompt_move[move_prompt_index].message,
            GET_NAME(group_member));
    }
}

void display_joined_group(char_data* character, char_data* leader)
{
    act("You join the group of $N.", FALSE, character, 0, leader, TO_CHAR);
    act("$n joins your group.", TRUE, character, 0, leader, TO_VICT);
    act("$n joins the group of $N.", TRUE, character, 0, leader, TO_NOTVICT);
}

void add_character_to_group(char_data* character, char_data* group_leader)
{
    group_data* group = group_leader->group;
    if (group == NULL) {
        group = new group_data(group_leader);
    }

    group->add_member(character);

    act("You join the group of $N.", FALSE, character, 0, group_leader, TO_CHAR);
    act("$n joins your group.", TRUE, character, 0, group_leader, TO_VICT);
    act("$n joins the group of $N.", TRUE, character, 0, group_leader, TO_NOTVICT);
}

void remove_character_from_group(char_data* character, char_data* group_leader)
{
    if (group_leader->group == NULL)
        return;

    group_data* group = group_leader->group;
    group->remove_member(character);

    act("You leave the group of $N.", FALSE, character, 0, group_leader, TO_CHAR);
    act("$n leaves the group of $N.", FALSE, character, 0, group_leader, TO_NOTVICT);
    act("$n leaves your group.", FALSE, character, 0, group_leader, TO_VICT);

    // We removed the last member from the group, destroy the group.
    if (group->size() == 1) {
        send_to_char("You have disbanded the group.\n\r", group_leader);
        group_leader->group = NULL;
        delete group;
    }
}

ACMD(do_group)
{
    one_argument(argument, buf);

    if (IS_SHADOW(ch)) {
        send_to_char("You cannot group whilst inhabiting the shadow world.\n\r", ch);
        return;
    }

    // If only "group" was passed in.
    if (string_func::is_null_or_empty(buf)) {
        if (ch->group == NULL) {
            send_to_char("But you are not the member of a group!\n\r", ch);
        } else {
            send_to_char("Your group consists of:\n\r", ch);

            // first, display the group leader's status
            char_data* leader = ch->get_group_leader();
            print_group_leader(leader);
            send_to_char(buf, ch);

            // now, we display the group members; start with index 1 because index 0 is the group
            // leader
            for (size_t index = 1; index < ch->group->size(); ++index) {
                char_data* group_member = ch->group->at(index);
                print_group_member(group_member);
                send_to_char(buf, ch);
            }
        }
    } else {
        // Try to add members to the group.
        group_data* group = ch->group;
        bool can_add_members = group == NULL;
        if (!can_add_members) {
            can_add_members = group->is_leader(ch);
        }

        if (!can_add_members) {
            act("You can not enroll group members without being head of a group.", FALSE, ch, 0, 0,
                TO_CHAR);
            return;
        }

        if (string_func::equals(buf, "all")) {
            for (follow_type* follower = ch->followers; follower; follower = follower->next) {
                char_data* potential_member = follower->follower;
                if (potential_member && char_exists(follower->fol_number)) {
                    // Can only add ungrouped members to your group.
                    if (potential_member->group == NULL && !other_side(ch, potential_member)) {
                        add_character_to_group(potential_member, ch);
                    }
                } else {
                    // Follower clean-up is done here.  Unsure why, don't want to change
                    // functionality though.
                    follower->follower = NULL;
                }
            }
            return;
        }

        if (char_data* potential_member = get_char_room_vis(ch, buf)) {
            // Easy error cases are handled up here.
            if (potential_member == ch) {
                send_to_char("Eww, who wants to group with that guy?\n\r", ch);
            } else if (other_side(ch, potential_member)) {
                sprintf(buf, "You wouldn't group with that ugly %s!\n\r",
                    pc_races[GET_RACE(potential_member)]);
                send_to_char(buf, ch);
            } else if (potential_member->group && potential_member->group != group) {
                act("$N is busy somewhere else already.", FALSE, ch, 0, potential_member, TO_CHAR);
            } else {
                if (group == NULL) {
                    group = new group_data(ch);
                    ch->group = group;
                }

                char_iter member = std::find(group->begin(), group->end(), potential_member);
                if (member == group->end()) {
                    add_character_to_group(potential_member, ch);
                } else {
                    remove_character_from_group(potential_member, ch);
                }
            }
        } else {
            send_to_char("No one here by that name.\n\r", ch);
        }
    }
}

ACMD(do_ungroup)
{
    one_argument(argument, buf);

    group_data* group = ch->group;

    // Ungroup was typed in without an argument.
    if (string_func::is_null_or_empty(buf)) {
        if (group) {
            if (group->is_leader(ch)) {
                size_t index = group->size() - 1;
                while (index >= 1) {
                    remove_character_from_group(group->at(index--), ch);
                }
            } else {
                remove_character_from_group(ch, group->get_leader());
            }
        } else {
            send_to_char("You have no group to leave.\n\r", ch);
        }
    } else {
        // Trying to ungroup a particular character.
        if (!group || !group->is_leader(ch)) {
            send_to_char("You are not the leader of a group.\n\r", ch);
        } else {
            if (char_data* lost_member = get_char_room_vis(ch, buf)) {
                if (group->contains(ch)) {
                    act("You have been kicked out of $n's group!", FALSE, ch, 0, lost_member,
                        TO_VICT);
                    remove_character_from_group(lost_member, ch);
                } else {
                    send_to_char("That person is not in your group!\n\r", ch);
                }
            } else {
                send_to_char("There is no such person!\n\r", ch);
            }
        }
    }
}

ACMD(do_report)
{
    char str[255];
    int tmp1, tmp2, tmp3;
    char* tmpchar;

    extern struct prompt_type prompt_hit[];
    extern struct prompt_type prompt_mana[];
    extern struct prompt_type prompt_move[];

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET) && !(MOB_FLAGGED(ch, MOB_ORC_FRIEND))) {
        send_to_char("Sorry, tamed mobiles can't report.\n\r", ch);
        return;
    }

    for (tmp1 = 0; (1000 * GET_HIT(ch)) / GET_MAX_HIT(ch) > prompt_hit[tmp1].value; tmp1++)
        ;
    for (tmp2 = 0; (1000 * GET_MANA(ch)) / GET_MAX_MANA(ch) > prompt_mana[tmp2].value; tmp2++)
        ;
    for (tmp3 = 0; (1000 * GET_MOVE(ch)) / GET_MAX_MOVE(ch) > prompt_move[tmp3].value; tmp3++)
        ;

    sprintf(
        str, "I am %s, my stamina is %s, and I am %s.",
        prompt_hit[tmp1].message, // No need to add
        *prompt_mana[tmp2].message == '\0' ? "full"
                                           : prompt_mana[tmp2].message + 3, // Add 3 because of M:
        *prompt_move[tmp3].message == '\0' ? "energetic"
                                           : prompt_move[tmp3].message + 4); // Add 4 because of MV:

    for (tmpchar = &str[1]; *tmpchar != '\0'; tmpchar++)
        *tmpchar = tolower(*tmpchar);

    if (ch->group) {
        do_gsay(ch, str, 0, 0, 0);
    } else {
        do_say(ch, str, 0, 0, 0);
    }
}

int calculate_gold_amount(char* text, char* argument, char_data* character)
{
    char* current_argument = one_argument(argument, text);

    int amount = 0;
    if (is_number(text)) {
        amount = atoi(text);
        if (amount > 0) {
            one_argument(current_argument, text);
            if (string_func::equals(text, "gold") || string_func::equals(text, "silver") || string_func::equals(text, "copper")) {
                /* save some strcmp'ing time since only the previous three
                 * arguments could possibly have made it here */
                switch (*text) {
                case 'g':
                    amount *= 1000;
                    break;
                case 's':
                    amount *= 100;
                    break;
                case 'c':
                    amount *= 1;
                    break;
                }
            } else {
                send_to_char("You must specify what type of coin to split.\r\n", character);
            }
        } else {
            send_to_char("Sorry, you can't do that.\n\r", character);
        }
    } else {
        send_to_char("You must specify how much you wish to split.\r\n", character);
    }

    return amount;
}

void give_share(char_data* sender, char_data* receiver, int share_amount)
{
    GET_GOLD(receiver) += share_amount;
    sprintf(buf, "%s splits some money among the group; you receive %s.\r\n", GET_NAME(sender),
        money_message(share_amount, 0));
    send_to_char(buf, receiver);
}

ACMD(do_split)
{
    if (IS_NPC(ch))
        return;

    if (ch->group == NULL) {
        send_to_char("You have no group to split with.\n\r", ch);
        return;
    }

    int total_gold_to_split = calculate_gold_amount(buf, argument, ch);
    if (total_gold_to_split == 0)
        return;

    if (total_gold_to_split > GET_GOLD(ch)) {
        send_to_char("You don't have that much coin to split.\n\r", ch);
        return;
    }

    char_vector split_group;
    ch->group->get_pcs_in_room(split_group, ch->in_room);
    int share_count = (int)split_group.size();
    if (share_count == 1) {
        send_to_char("You have no one to split with.\n\r", ch);
        return;
    }

    int share_amount = total_gold_to_split / share_count;
    GET_GOLD(ch) -= share_amount * (share_count - 1);

    for (char_iter iter = split_group.begin(); iter != split_group.end(); ++iter) {
        char_data* receiver = *iter;
        if (receiver != ch) {
            give_share(ch, receiver, share_amount);
        }
    }

    sprintf(buf, "You give %s to each member of your group.\n\r", money_message(share_amount, 0));
    send_to_char(buf, ch);
}

ACMD(do_use)
{
    int bits;
    struct char_data* tmp_char;
    struct obj_data *tmp_object, *stick;

    argument = one_argument(argument, buf);

    if (ch->equipment[HOLD] == 0 || !isname(buf, ch->equipment[HOLD]->name)) {
        act("You do not hold that item in your hand.", FALSE, ch, 0, 0, TO_CHAR);
        return;
    }

    stick = ch->equipment[HOLD];

    if (stick->obj_flags.type_flag == ITEM_STAFF) {
        act("$n taps $p three times on the ground.", TRUE, ch, stick, 0, TO_ROOM);
        act("You tap $p three times on the ground.", FALSE, ch, stick, 0, TO_CHAR);

        if (stick->obj_flags.value[2] > 0) { /* Is there any charges left? */
            stick->obj_flags.value[2]--;
            if (*skills[stick->obj_flags.value[3]].spell_pointer)
                ((*skills[stick->obj_flags.value[3]].spell_pointer)(ch, "", SPELL_TYPE_STAFF, 0, 0,
                    0, 0));

        } else
            send_to_char("The staff seems powerless.\n\r", ch);
    } else if (stick->obj_flags.type_flag == ITEM_WAND) {
        bits = generic_find(argument, FIND_CHAR_ROOM | FIND_OBJ_INV | FIND_OBJ_ROOM | FIND_OBJ_EQUIP,
            ch, &tmp_char, &tmp_object);
        if (bits) {
            if (bits == FIND_CHAR_ROOM) {
                act("$n points $p at $N.", TRUE, ch, stick, tmp_char, TO_ROOM);
                act("You point $p at $N.", FALSE, ch, stick, tmp_char, TO_CHAR);
            } else {
                act("$n points $p at $P.", TRUE, ch, stick, tmp_object, TO_ROOM);
                act("You point $p at $P.", FALSE, ch, stick, tmp_object, TO_CHAR);
            }

            if (stick->obj_flags.value[2] > 0) { /* Is there any charges left? */
                stick->obj_flags.value[2]--;
                if (*skills[stick->obj_flags.value[3]].spell_pointer)
                    ((*skills[stick->obj_flags.value[3]].spell_pointer)(
                        ch, "", SPELL_TYPE_WAND, tmp_char, tmp_object, 0, 0));
            } else
                send_to_char("The wand seems powerless.\n\r", ch);
        } else
            send_to_char("What should the wand be pointed at?\n\r", ch);
    } else
        send_to_char("Use is normally only for wands and staffs.\n\r", ch);
}

ACMD(do_wimpy)
{
    int wimp_lev;

    one_argument(argument, arg);

    if (!*arg) {
        if (WIMP_LEVEL(ch)) {
            sprintf(buf, "You will flee if your hit points drop below %d.\n\r",
                ch->specials2.wimp_level);
            send_to_char(buf, ch);
            return;
        } else {
            send_to_char("You will not flee from combat.\n\r", ch);
            return;
        }
    }

    if (isdigit(*arg)) {
        if ((wimp_lev = atoi(arg))) {
            if (wimp_lev < 0)
                wimp_lev = 0;
            if (wimp_lev > GET_MAX_HIT(ch))
                wimp_lev = GET_MAX_HIT(ch);
            sprintf(buf, "OK, you'll flee if you drop below %d hit points.\n\r", wimp_lev);
            send_to_char(buf, ch);
            WIMP_LEVEL(ch) = wimp_lev;
        } else {
            send_to_char("OK, you'll tough out fights to the bitter end.\n\r", ch);
            WIMP_LEVEL(ch) = 0;
        }
    } else
        send_to_char("At how many hit points do you wish to flee?\n\r", ch);
}

char* logtypes[] = { "off", "brief", "normal", "spell", "complete", "\n" };

ACMD(do_syslog)
{
    int tp;

    if (IS_NPC(ch))
        return;

    one_argument(argument, arg);

    if (!*arg) {
        tp = ((PRF_FLAGGED(ch, PRF_LOG1) ? 1 : 0) + (PRF_FLAGGED(ch, PRF_LOG2) ? 2 : 0) + (PRF_FLAGGED(ch, PRF_LOG3) ? 4 : 0));
        sprintf(buf, "Your syslog is currently %s.\n\r", logtypes[tp]);
        send_to_char(buf, ch);
        return;
    }

    if (((tp = search_block(arg, logtypes, FALSE)) == -1)) {
        send_to_char("Usage: syslog { Off | Brief | Normal | Spell | Complete }\n\r", ch);
        return;
    }

    REMOVE_BIT(PRF_FLAGS(ch), PRF_LOG1 | PRF_LOG2 | PRF_LOG3);
    SET_BIT(PRF_FLAGS(ch),
        (PRF_LOG1 * (tp & 1)) | (PRF_LOG2 * (tp & 2) >> 1) | (PRF_LOG3 * (tp & 4) >> 2));

    sprintf(buf, "Your syslog is now %s.\n\r", logtypes[tp]);
    send_to_char(buf, ch);
}

#define TOG_OFF 0
#define TOG_ON 1

#define PRF_TOG_CHK(ch, flag) ((TOGGLE_BIT(PRF_FLAGS(ch), (flag))) & (flag))

int flag_on(struct char_data* ch, int flag, char** message, int which)
{
    if (!which)
        SET_BIT(PRF_FLAGS(ch), (flag));
    else
        SET_BIT(PLR_FLAGS(ch), (flag));

    send_to_char(message[0], ch);
    return 1;
}

int flag_off(struct char_data* ch, int flag, char** message, int which)
{
    if (!which)
        REMOVE_BIT(PRF_FLAGS(ch), (flag));
    else
        REMOVE_BIT(PLR_FLAGS(ch), (flag));
    send_to_char(message[1], ch);
    return 0;
}

int flag_toggle(struct char_data* ch, int flag, char** message, int which)
{
    int i;
    if (!which) {
        TOGGLE_BIT(PRF_FLAGS(ch), (flag));
        i = IS_SET(PRF_FLAGS(ch), (flag));
    } else {
        TOGGLE_BIT(PLR_FLAGS(ch), (flag));
        i = IS_SET(PLR_FLAGS(ch), (flag));
    }
    if (i)
        send_to_char(message[0], ch);
    else
        send_to_char(message[1], ch);
    return i;
}

int flag_void(struct char_data* ch, int flag, char** message, int which)
{
    int i;
    if (!which)
        i = IS_SET(PRF_FLAGS(ch), (flag));
    else
        i = IS_SET(PLR_FLAGS(ch), (flag));

    if (i)
        send_to_char(message[2], ch);
    else
        send_to_char(message[3], ch);

    return i;
}

int (*flag_modify)(struct char_data*, int, char**, int);
char* tog_messages[][4] = {
    { "You are now safe from summoning by other players.\n\r",
        "You may now be summoned by other players.\n\r",
        "You are safe from summoning by other players.\n\r",
        "You may be summoned by other players.\n\r" },
    {
        "You will now have your communication repeated.\r\n",
        "You will no longer have your communication repeated.\r\n",
        "You have your communication repeated.\r\n",
        "You don't have your communication repeated.\r\n",
    },
    { "Brief mode now on.\n\r", "Brief mode now off.\n\r", "Brief mode on.\n\r",
        "Brief mode off.\n\r" },
    { "Spam mode now on.\n\r", "Spam mode now off.\n\r", "Spam mode on.\n\r", "Spam mode off.\n\r" },
    { "Compact mode now on.\n\r", "Compact mode now off.\n\r", "Compact mode on.\n\r",
        "Compact mode off.\n\r" },
    { "You are now deaf to tells.\n\r", "You can now hear tells.\n\r", "You are deaf to tells.\n\r",
        "You can hear tells.\n\r" },
    { "You can now hear narrates.\n\r", "You are now deaf to narrates.\n\r",
        "You can hear narrates.\n\r", "You are deaf to narrates.\n\r" },
    { "You can now hear chat.\n\r", "You are now deaf to chat.\n\r", "You can hear chat.\n\r",
        "You are deaf to chat.\n\r" },
    { "You can now hear the wiz-channel.\n\r", "You are now deaf to the wiz-channel.\n\r",
        "You can hear the wiz-channel.\n\r", "You are deaf to the wiz-channel.\n\r" },
    { "You will now see the room flags.\n\r", "You will no longer see the room flags.\n\r",
        "You see the room flags.\n\r", "You don't see the room flags.\n\r" },
    { "Nohassle now enabled.\n\r", "Nohassle now disabled.\n\r", "Nohassle enabled.\n\r",
        "Nohassle disabled.\n\r" },
    { "Holylight mode is now on.\n\r", "Holylight mode is now off.\n\r", "Holylight mode is on.\n\r",
        "Holylight mode is off.\n\r" },
    { "Nameserver_is_slow changed to NO; IP addresses will now be resolved.\n\r",
        "Nameserver_is_slow changed to YES; sitenames will no longer be resolved.\n\r",
        "Nameserver_is_slow is set to NO; IP addresses will be resolved.\n\r",
        "Nameserver_is_slow is set to YES; sitenames will not be resolved.\n\r" },
    { "Line wrapping is now on.\n\r", "Line wrapping is now off.\n\r", "Line wrapping is on.\n\r",
        "Line wrapping is off.\n\r" },
    { "Msdp mode is now on.\n\r", "Msdp mode is now off.\n\r", "Msdp mode is on.\n\r",
        "Msdp mode is off.\n\r" },
    { "AutoMental mode is now on.\n\r", "AutoMental mode is now off.\n\r",
        "AutoMental mode is on.\n\r", "AutoMental mode is off.\n\r" },
    { "Incognito mode is now on.\n\r", "Incognito mode is now off.\n\r", "Incognito mode is on.\n\r",
        "Incognito mode is off.\n\r" },
    { "You can now hear singing.\n\r", "You are now deaf to singing.\n\r",
        "You can hear singing.\n\r", "You are deaf to singing.\n\r" },
    { "Prompt is now on.\n\r", "Prompt is now off.\n\r", "Prompt is on.\n\r", "Prompt is off.\n\r" },
    { "You will now attempt to swim if needed.\r\n", "You will no longer attempt to swim.\r\n",
        "You swim when needed.\r\n", "You refuse to swim.\r\n" },
    { "You will now use the latin-1 character set.\r\n",
        "You will no longer use the latin-1 character set.\r\n",
        "You are currently using the latin-1 character set.\r\n",
        "You are currently using the standard character set.\r\n" },
    { "You will now see a spinner during skill and spell delays.\r\n",
        "You will no longer see a spinner during skill and spell delays.\r\n",
        "You currently see a spinner during skill and spell delays.\r\n",
        "You currently do not see a spinner during skill and spell delays.\r\n" },
    { "Advanced view is now on.\r\n", "Advanced view is off.\r\n", "Advanced view is on.\r\n",
        "Advanced view is off.\r\n" },
    { "Advanced prompt is now on.\r\n", "Advanced prompt is off.\r\n", "Advanced prompt is on.\r\n",
        "Advanced prompt is off.\r\n" }
};

ACMD(do_gen_tog)
{
    long result;
    char str[10];
    int len, mod, tmp;

    extern int nameserver_is_slow;

    if (IS_NPC(ch))
        return;

    len = strlen(argument);
    mod = 0;

    if (len) {
        for (tmp = 0; tmp < len && tmp < 9; tmp++)
            str[tmp] = tolower(argument[tmp]);
        str[tmp] = 0;
        len = tmp;
        if (!strncmp("on", str, len)) {
            mod = 1;
            flag_modify = flag_on;
        }
        if (!strncmp("off", str, len)) {
            mod = 2;
            flag_modify = flag_off;
        }
        if (!strncmp("toggle", str, len)) {
            mod = 3;
            flag_modify = flag_toggle;
        }
        if (!mod) {
            send_to_char("Options are on | off | toggle.\n\r", ch);
            return;
        }
    } else {
        mod = 0;
        flag_modify = flag_void;
    }

    switch (subcmd) {
    case SCMD_SPINNER:
        result = flag_modify(ch, PRF_SPINNER, tog_messages[21], 0);
        break;

    case SCMD_LATIN1:
        result = flag_modify(ch, PRF_LATIN1, tog_messages[20], 0);
        break;

    case SCMD_NOSUMMON:
        result = flag_modify(ch, PRF_SUMMONABLE, tog_messages[0], 0);
        break;

    case SCMD_ADVANCED_VIEW:
        result = flag_modify(ch, PRF_ADVANCED_VIEW, tog_messages[22], 0);
        break;

    case SCMD_ADVANCED_PROMPT:
        result = flag_modify(ch, PRF_ADVANCED_PROMPT, tog_messages[23], 0);
        break;

    case SCMD_ECHO:
        result = flag_modify(ch, PRF_ECHO, tog_messages[1], 0);
        break;

    case SCMD_BRIEF:
        result = flag_modify(ch, PRF_BRIEF, tog_messages[2], 0);
        break;

    case SCMD_SPAM:
        result = flag_modify(ch, PRF_SPAM, tog_messages[3], 0);
        break;

    case SCMD_COMPACT:
        result = flag_modify(ch, PRF_COMPACT, tog_messages[4], 0);
        break;

    case SCMD_NOTELL:
        result = flag_modify(ch, PRF_NOTELL, tog_messages[5], 0);
        break;

    case SCMD_NARRATE:
        result = flag_modify(ch, PRF_NARRATE, tog_messages[6], 0);
        break;

    case SCMD_CHAT:
        result = flag_modify(ch, PRF_CHAT, tog_messages[7], 0);
        break;

    case SCMD_INCOGNITO:
        result = flag_modify(ch, PLR_INCOGNITO, tog_messages[16], 1);
        break;

    case SCMD_SING:
        result = flag_modify(ch, PRF_SING, tog_messages[17], 0);
        break;

    case SCMD_SETPROMPT:
        result = flag_modify(ch, PRF_PROMPT, tog_messages[18], 0);
        break;

    case SCMD_WIZ:
        result = flag_modify(ch, PRF_WIZ, tog_messages[8], 0);
        break;

    case SCMD_SWIM:
        result = flag_modify(ch, PRF_SWIM, tog_messages[19], 0);
        break;

    case SCMD_ROOMFLAGS:
        result = flag_modify(ch, PRF_ROOMFLAGS, tog_messages[9], 0);
        break;

    case SCMD_NOHASSLE:
        result = flag_modify(ch, PRF_NOHASSLE, tog_messages[10], 0);
        break;

    case SCMD_HOLYLIGHT:
        result = flag_modify(ch, PRF_HOLYLIGHT, tog_messages[11], 0);
        break;

    case SCMD_SLOWNS:
        result = (nameserver_is_slow = !nameserver_is_slow);
        if (!result)
            send_to_char(tog_messages[12][2], ch);
        else
            send_to_char(tog_messages[12][3], ch);
        break;

    case SCMD_WRAP:
        result = flag_modify(ch, PRF_WRAP, tog_messages[13], 0);
        break;

    case SCMD_MSDP:
        result = flag_modify(ch, PRF_MSDP, tog_messages[14], 0);
        break;

    case SCMD_MENTAL:
        result = flag_modify(ch, PRF_MENTAL, tog_messages[15], 0);
        break;

    default:
        log("SYSERR: Unknown subcmd in do_gen_toggle");
        return;
        break;
    }
}

extern char* casting[];
ACMD(do_casting)
{
    if (GET_SPEC(ch) != PLRSPEC_ARCANE) {
        send_to_char("Only players specialized in arcane may set their casting speed.\n\r", ch);
        return;
    }
    char* s1 = "You are presently using";
    char* s2 = "You are now using";
    char* s;
    int tmp, len;
    if (!*argument) {
        s = s1;
    } else {
        s = s2;
        len = strlen(argument);

        for (tmp = 0; casting[tmp][0] != '\n'; tmp++) {
            if (!strncmp(casting[tmp], argument, len)) {
                break;
            }
        }

        if (casting[tmp][0] == '\n') {
            sprintf(buf, "Possible casting modes are:\n\r   ");
            for (tmp = 0; casting[tmp][0] != '\n'; tmp++) {
                strcat(buf, casting[tmp]);
                strcat(buf, " casting.");
                strcat(buf, "\n\r    ");
            }
            send_to_char(buf, ch);
            return;
        }

        switch (tmp) {
        case 0:
            SET_CASTING(ch, CASTING_SLOW);
            break;
        case 1:
            SET_CASTING(ch, CASTING_NORMAL);
            break;
        case 2:
            SET_CASTING(ch, CASTING_FAST);
            break;
        default:
            SET_CASTING(ch, 99);
            break;
        }
    }
    switch (GET_CASTING(ch)) {
    case CASTING_SLOW:
        sprintf(buf, "%s %s casting.\n\r", s, casting[0]);
        break;
    case CASTING_NORMAL:
        sprintf(buf, "%s %s casting.\n\r", s, casting[1]);
        break;
    case CASTING_FAST:
        sprintf(buf, "%s %s casting.\n\r", s, casting[2]);
        break;
    default:
        sprintf(buf, "%s weird.\n\r", s);
        break;
    }
    send_to_char(buf, ch);
}

extern char* shooting[];
ACMD(do_shooting)
{
    if (GET_SPEC(ch) != PLRSPEC_ARCH) {
        send_to_char("Only players specialized in archery may set their speed.\n\r", ch);
        return;
    }
    char* s1 = "You are presently using";
    char* s2 = "You are now using";

    char* s;
    int tmp, len;
    if (!*argument)
        s = s1;
    else {
        s = s2;
        len = strlen(argument);

        for (tmp = 0; shooting[tmp][0] != '\n'; tmp++) {
            if (!strncmp(shooting[tmp], argument, len)) {
                break;
            }
        }

        if (shooting[tmp][0] == '\n') {
            sprintf(buf, "Possible shoot modes are:\n\r   ");
            for (tmp = 0; shooting[tmp][0] != '\n'; tmp++) {
                strcat(buf, shooting[tmp]);
                strcat(buf, " shooting.");
                strcat(buf, "\n\r    ");
            }
            send_to_char(buf, ch);
            return;
        }

        switch (tmp) {
        case 0:
            SET_SHOOTING(ch, SHOOTING_SLOW);
            break;
        case 1:
            SET_SHOOTING(ch, SHOOTING_NORMAL);
            break;
        case 2:
            SET_SHOOTING(ch, SHOOTING_FAST);
            break;
        default:
            SET_SHOOTING(ch, 99);
            break;
        }
    }

    switch (GET_SHOOTING(ch)) {
    case SHOOTING_SLOW:
        sprintf(buf, "%s %s shooting.\n\r", s, shooting[0]);
        break;
    case SHOOTING_NORMAL:
        sprintf(buf, "%s %s shooting.\n\r", s, shooting[1]);
        break;
    case SHOOTING_FAST:
        sprintf(buf, "%s %s shooting.\n\r", s, shooting[2]);
        break;
    default:
        sprintf(buf, "%s weird.\n\r", s);
        break;
    }
    send_to_char(buf, ch);
}

std::array<std::string_view, 4> inv_sorting = { "default", "grouped", "alpha", "length" };

namespace {
bool has_argument(const char* argument) { return *argument != 0; }

int get_sort_index(const char* argument)
{
    for (int index = 0; index < inv_sorting.size(); ++index) {
        if (inv_sorting[index].find(argument) != std::string::npos) {
            return index;
        }
    }

    return -1;
}

void set_sort_value(int sort_index, char_data* character)
{
    int high_bit_value = (sort_index & 2) >> 1;
    int low_bit_value = sort_index & 1;

    if (high_bit_value != 0) {
        SET_BIT(character->specials2.pref, PRF_INV_SORT2);
    } else {
        REMOVE_BIT(character->specials2.pref, PRF_INV_SORT2);
    }

    if (low_bit_value != 0) {
        SET_BIT(character->specials2.pref, PRF_INV_SORT1);
    } else {
        REMOVE_BIT(character->specials2.pref, PRF_INV_SORT1);
    }
}

void report_sort_choices_to(char_data* character)
{
    std::ostringstream message_writer;
    message_writer << "Possible sort choices are:" << std::endl;
    for (const auto& sort_name : inv_sorting) {
        message_writer << "\t" << sort_name << std::endl;
    }
    message_writer << std::endl;

    send_to_char(message_writer.str().c_str(), character);
}

void report_inventory_sorting_to(char_data* character, const char* intro_string)
{
    bool high_bit_set = PRF_FLAGGED(character, PRF_INV_SORT2) != 0;
    bool low_bit_set = PRF_FLAGGED(character, PRF_INV_SORT1) != 0;

    int sort_value = high_bit_set << 1 | low_bit_set;

    const char* sort_name = inv_sorting[sort_value].data();

    sprintf(buf, "%s %s.\r\n", intro_string, sort_name);
    send_to_char(buf, character);
}
} // namespace

ACMD(do_inventory_sort)
{
    if (has_argument(argument)) {
        int sort_index = get_sort_index(argument);
        if (sort_index >= 0) {
            set_sort_value(sort_index, ch);

            static const char* report_new_sort_string = "Your new inventory sorting method is";
            report_inventory_sorting_to(ch, report_new_sort_string);
        } else {
            report_sort_choices_to(ch);
        }
    } else {
        static const char* report_sort_string = "Your current inventory sorting method is";
        report_inventory_sorting_to(ch, report_sort_string);
        report_sort_choices_to(ch);
    }
}

extern char* tactics[];
ACMD(do_tactics)
{
    char* s1 = "You are presently employing";
    char* s2 = "You are now employing";
    char* s;
    int tmp, len;

    if (utils::is_affected_by_spell(*ch, SKILL_FRENZY) && utils::get_race(*ch) == RACE_OLOGHAI) {
        send_to_char("The rage inside you won't let you cool down!\r\n", ch);
        return;
    }

    if (!*argument)
        s = s1;
    else {
        s = s2;
        len = strlen(argument);

        for (tmp = 0; tactics[tmp][0] != '\n'; tmp++)
            if (!strncmp(tactics[tmp], argument, len))
                break;

        if (tactics[tmp][0] == '\n') {
            sprintf(buf, "Possible tactics are:\n\r   ");
            for (tmp = 0; tactics[tmp][0] != '\n'; tmp++) {
                strcat(buf, tactics[tmp]);
                strcat(buf, " tactics.");
                strcat(buf, "\n\r    ");
            }
            send_to_char(buf, ch);
            return;
        }

        if ((GET_TACTICS(ch) == TACTICS_BERSERK)) {
            if (number(-20, 100) > GET_RAW_SKILL(ch, SKILL_BERSERK)) {
                send_to_char("You failed to cool down.\n\r", ch);
                return;
            }
        }

        player_spec::wild_fighting_handler wild_fighting(ch);
        int target_tactics = TACTICS_NORMAL;

        switch (tmp) {
        case 0:
            target_tactics = TACTICS_DEFENSIVE;
            break;

        case 1:
            target_tactics = TACTICS_CAREFUL;
            break;

        case 2:
            target_tactics = TACTICS_NORMAL;
            break;

        case 3:
            target_tactics = TACTICS_AGGRESSIVE;
            break;

        case 4:
            target_tactics = TACTICS_BERSERK;
            break;

        default:
            target_tactics = 99;
            break;
        }

        SET_TACTICS(ch, target_tactics);
        wild_fighting.update_tactics(target_tactics);
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        sprintf(buf, "%s %s tactics.\n\r", s, tactics[0]);
        break;

    case TACTICS_CAREFUL:
        sprintf(buf, "%s %s tactics.\n\r", s, tactics[1]);
        break;

    case TACTICS_NORMAL:
        sprintf(buf, "%s %s tactics.\n\r", s, tactics[2]);
        break;

    case TACTICS_AGGRESSIVE:
        sprintf(buf, "%s %s tactics.\n\r", s, tactics[3]);
        break;

    case TACTICS_BERSERK:
        sprintf(buf, "%s %s tactics.\n\r", s, tactics[4]);
        break;

    default:
        sprintf(buf, "%s weird.\n\r", s);
        break;
    }
    send_to_char(buf, ch);
}

ACMD(do_language)
{
    char str[200];
    int len, tmp;

    if (!*argument) {
        sprintf(str, "You are using %s.\n\r",
            (ch->player.language) ? skills[ch->player.language].name : "common language");
        send_to_char(str, ch);
        return;
    }
    len = strlen(argument);

    if (!strncmp(argument, "common language", len)) {
        if (GET_LEVEL(ch) < LEVEL_IMMORT)
            send_to_char("You can't speak in the common tongue.\n\r", ch);
        else {
            ch->player.language = 0;
            send_to_char("You will now use common language.\n\r", ch);
        }
        return;
    }

    for (tmp = 0; tmp < language_number; tmp++)
        if (!strncmp(skills[language_skills[tmp]].name, argument, len))
            break;

    if (tmp == language_number) {
        strcpy(str, "Possible languages are:\n\r");
        for (tmp = 0; tmp < language_number; tmp++) {
            strcat(str, skills[language_skills[tmp]].name);
            strcat(str, "\n\r");
        }
        send_to_char(str, ch);
        return;
    }

    ch->player.language = language_skills[tmp];
    sprintf(str, "You will now use %s.\n\r", skills[language_skills[tmp]].name);
    send_to_char(str, ch);
    return;
}

#define SORTING_COMMAND_INDEX 30

char* change_comm[] = {
    "prompt", /* 0 */
    "tactics", "nosummon", "echo", "brief", "spam", /* 5 */
    "compact", "notell", "narrate", "chat", "title", /* 10 */
    "wimpy", "wrap", "msdp", "language", "description", /* 15 */
    "incognito", "time", "sing", "mental", "swim", /* 20 */
    "latin1", "spinner", "wiz", "roomflags", "nohassle", /* 25 */
    "holylight", "slowns", "shooting", "casting", "sorting", /* 30 */
    "advancedview", "advancedprompt", "\n"
};

ACMD(do_toggle);
ACMD(do_title);
ACMD(do_wimpy);

ACMD(do_set)
{
    char command[50];
    char arg[250];

    // What the fuck is tmp and tmp2?!?!?!?!?
    int change_index, tmp2, len;

    if (IS_NPC(ch)) {
        send_to_char("Sorry, NPCs can't do that.\n\r", ch);
        return;
    }

    half_chop(argument, command, arg);

    len = strlen(command);

    if (!*command) {
        do_toggle(ch, "", wtl, 0, 0);
        return;
    }

    for (change_index = 0; change_comm[change_index][0] != '\n'; change_index++)
        if (!strncmp(change_comm[change_index], command, len))
            break;

    if (change_comm[change_index][0] == '\n') {
    no_set:
        sprintf(buf, "Possible arguments are:\n\r");
        /*
         * Such. a. fucking. hack.  This doesn't even really work: slowns
         * is implementor-only, as may be the case for other future
         * options.  We really need a generic type of list that can deal
         * with permissions based on level.
         */
        auto character_level = (GET_LEVEL(ch) >= LEVEL_IMMORT) ? 27 : 23;
        for (change_index = 0; change_index < character_level; change_index++) {
            strcat(buf, change_comm[change_index]);
            strcat(buf, ", ");
        }
        strcat(buf, "sorting, advancedview, advancedprompt");
        strcat(buf, ".\n\r");
        send_to_char(buf, ch);
        return;
    }

    switch (change_index) {
    case 0:
        do_gen_tog(ch, arg, wtl, 0, SCMD_SETPROMPT);
        break;

    case 1:
        do_tactics(ch, arg, wtl, 0, 0);
        break;

    case 2:
        do_gen_tog(ch, arg, wtl, 0, SCMD_NOSUMMON);
        break;

    case 3:
        do_gen_tog(ch, arg, wtl, 0, SCMD_ECHO);
        break;

    case 4:
        do_gen_tog(ch, arg, wtl, 0, SCMD_BRIEF);
        break;

    case 5:
        do_gen_tog(ch, arg, wtl, 0, SCMD_SPAM);
        break;

    case 6:
        do_gen_tog(ch, arg, wtl, 0, SCMD_COMPACT);
        break;

    case 7:
        do_gen_tog(ch, arg, wtl, 0, SCMD_NOTELL);
        break;

    case 8:
        do_gen_tog(ch, arg, wtl, 0, SCMD_NARRATE);
        break;

    case 9:
        do_gen_tog(ch, arg, wtl, 0, SCMD_CHAT);
        break;

    case 10:
        do_title(ch, arg, wtl, 0, 0);
        break;

    case 11:
        do_wimpy(ch, arg, wtl, 0, 0);
        break;

    case 12:
        do_gen_tog(ch, arg, wtl, 0, SCMD_WRAP);
        break;

    case 13:
        do_gen_tog(ch, arg, wtl, 0, SCMD_MSDP); // TODO: Remove this
        break;

    case 14:
        do_language(ch, arg, wtl, 0, 0);
        break;

    case 15:
        string_add_init(ch->desc, &(ch->player.description));
        break;

    case 16:
        if (GET_LEVEL(ch) < LEVEL_IMMORT)
            do_gen_tog(ch, arg, wtl, 0, SCMD_INCOGNITO);
        /* if the immortal is incog, then they can use incog to set it off */
        else if (IS_SET(PLR_FLAGS(ch), PLR_INCOGNITO))
            do_gen_tog(ch, arg, wtl, 0, SCMD_INCOGNITO);
        else
            send_to_char("Immortals cannot be incognito.\n\r", ch);
        break;

    case 17:
        do_gen_tog(ch, arg, wtl, 0, SCMD_TIME);
        break;

    case 18:
        do_gen_tog(ch, arg, wtl, 0, SCMD_SING);
        break;

    case 19:
        do_gen_tog(ch, arg, wtl, 0, SCMD_MENTAL);
        break;

    case 20:
        do_gen_tog(ch, arg, wtl, 0, SCMD_SWIM);
        break;

    case 21:
        do_gen_tog(ch, arg, wtl, 0, SCMD_LATIN1);
        break;

    case 22:
        do_gen_tog(ch, arg, wtl, 0, SCMD_SPINNER);
        break;

    case 23:
        if (GET_LEVEL(ch) >= LEVEL_IMMORT)
            do_gen_tog(ch, arg, wtl, 0, SCMD_WIZ);
        else
            goto no_set;
        break;

    case 24:
        if (GET_LEVEL(ch) >= LEVEL_IMMORT)
            do_gen_tog(ch, arg, wtl, 0, SCMD_ROOMFLAGS);
        else
            goto no_set;
        break;

    case 25:
        if (GET_LEVEL(ch) >= LEVEL_IMMORT)
            do_gen_tog(ch, arg, wtl, 0, SCMD_NOHASSLE);
        else
            goto no_set;
        break;

    case 26:
        if (GET_LEVEL(ch) >= LEVEL_IMMORT)
            do_gen_tog(ch, arg, wtl, 0, SCMD_HOLYLIGHT);
        else
            goto no_set;
        break;

    case 27:
        if (GET_LEVEL(ch) == LEVEL_IMPL)
            do_gen_tog(ch, arg, wtl, 0, SCMD_SLOWNS);
        else if (GET_LEVEL(ch) >= LEVEL_IMMORT)
            send_to_char("This is for implementors to decide.\n\r", ch);
        else
            goto no_set;
        break;

    case 28:
        do_shooting(ch, arg, wtl, 0, 0);
        break;

    case 29:
        do_casting(ch, arg, wtl, 0, 0);
        break;
    case SORTING_COMMAND_INDEX:
        do_inventory_sort(ch, arg, wtl, 0, 0);
        break;
    case 31:
        do_gen_tog(ch, arg, wtl, 0, SCMD_ADVANCED_VIEW);
        break;
    case 32:
        do_gen_tog(ch, arg, wtl, 0, SCMD_ADVANCED_PROMPT);
        break;
    default:
        send_to_char("Undefined response to this argument.\n\r", ch);
        return;
    }
}

ACMD(do_next)
{
    send_to_char("You see no boards here.\n\r", ch);
    return;
}

ACMD(do_knock)
{
    int tmp, room, oldroom;
    char str[200];

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    while ((*argument <= ' ') && (*argument))
        argument++;

    if (!*argument) {
        send_to_char("You knocked yourself on your head.\n\r", ch);
        act("$n knocked $mself on $s head.\n\r", FALSE, ch, 0, 0, TO_ROOM);
        return;
    }

    for (tmp = 0; tmp < NUM_OF_DIRS; tmp++) {
        if (EXIT(ch, tmp))
            if (!strcmp(argument, EXIT(ch, tmp)->keyword)) {
                if (IS_SET(EXIT(ch, tmp)->exit_info, EX_ISDOOR)) {
                    sprintf(str, "You knocked on the %s.\n", EXIT(ch, tmp)->keyword);
                    send_to_char(str, ch);
                    sprintf(str, "$n knocked on the %s.\n", EXIT(ch, tmp)->keyword);
                    act(str, FALSE, ch, 0, 0, TO_ROOM);

                    room = EXIT(ch, tmp)->to_room;
                    if (room != NOWHERE) {
                        oldroom = ch->in_room;
                        ch->in_room = room;
                        if (EXIT(ch, rev_dir[tmp])) {
                            if (IS_SET(EXIT(ch, rev_dir[tmp])->exit_info, EX_ISDOOR) && EXIT(ch, rev_dir[tmp])->keyword) {
                                sprintf(str, "You hear a knock on the %s.\n",
                                    EXIT(ch, rev_dir[tmp])->keyword);
                                act(str, FALSE, ch, 0, 0, TO_ROOM);
                            } else {
                                sprintf(str, "You hear a knock sounding %sward.\n\r",
                                    dirs[rev_dir[tmp]]);
                                act(str, FALSE, ch, 0, 0, TO_ROOM);
                            }
                        } else
                            act("You hear a distinct knocking sound.\n\r", FALSE, ch, 0, 0,
                                TO_ROOM);

                        ch->delay.targ1.type = TARGET_OTHER;
                        ch->delay.targ1.ch_num = rev_dir[tmp];
                        special(ch, CMD_KNOCK, argument, SPECIAL_NONE, 0);
                        ch->in_room = oldroom;
                    }

                    return;
                } else {
                    sprintf(str, "You waved your hand %sward.\n", dirs[tmp]);
                    send_to_char(str, ch);
                    sprintf(str, "$n waved $s hand %sward.\n", dirs[tmp]);
                    act(str, FALSE, ch, 0, 0, TO_ROOM);
                }
            }
    }
    send_to_char("There is nothing like that here.\n", ch);
}

ACMD(do_jig)
{
    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_SPEC)) {
        act("$n tries to jig, but decides against it.", TRUE, ch, 0, 0, TO_ROOM);
        return;
    }
    if (ch->specials.store_prog_number == 7) {
        act("$n stops dancing.", TRUE, ch, 0, 0, TO_ROOM);
        send_to_char("You stop dancing.\n\r", ch);
        ch->specials.store_prog_number = 0;
        return;
    }
    act("$n cheers and starts dancing a merry jig.", TRUE, ch, 0, 0, TO_ROOM);
    send_to_char("To celebrate, you dance a merry jig.\n\r", ch);
    ch->specials.store_prog_number = 7;
}

#define FIRST_BLOCK_PROC 8;

ACMD(do_block)
{
    int tmp, len;
    char str[255];

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    if (IS_NPC(ch) && mob_index[ch->nr].func) {
        send_to_char("You are too busy to do that.\n\r", ch);
        return;
    }

    if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_ORC_FRIEND)) {
        send_to_char("Sorry, you're not big enough for that.\n\r", ch);
        return;
    }

    while ((*argument <= ' ') && (*argument))
        argument++;

    if (!*argument) {
        tmp = ch->specials.store_prog_number - FIRST_BLOCK_PROC;

        if ((tmp < 0) || (tmp >= NUM_OF_DIRS)) {
            send_to_char("Which way do you want to block?\n\r", ch);
            return;
        } else {
            sprintf(buf, "You are blocking the way %s.\n\r", dirs[tmp]);
            send_to_char(buf, ch);
            return;
        }
    }

    one_argument(argument, str);
    len = strlen(str);

    for (tmp = 0; tmp < NUM_OF_DIRS; tmp++)
        if (!strncmp(dirs[tmp], str, len))
            break;

    if (tmp < NUM_OF_DIRS) {
        ch->specials.store_prog_number = tmp + FIRST_BLOCK_PROC;

        sprintf(buf, "You start blocking the way %s.\n\r", dirs[tmp]);
        send_to_char(buf, ch);
        sprintf(buf, "$n starts blocking the way %s.", dirs[tmp]);
        act(buf, TRUE, ch, 0, 0, TO_ROOM);
        return;
    }

    if (!strncmp(str, "none", len)) {
        tmp = ch->specials.store_prog_number - FIRST_BLOCK_PROC;
        if ((tmp >= 0) & (tmp < NUM_OF_DIRS)) {
            ch->specials.store_prog_number = 0;
            sprintf(buf, "You stop blocking the way %s.\n\r", dirs[tmp]);
            send_to_char(buf, ch);
            sprintf(buf, "$n stops blocking the way %s.", dirs[tmp]);
            act(buf, TRUE, ch, 0, 0, TO_ROOM);
        } else
            send_to_char("You are not blocking anything.\n\r", ch);
    } else
        send_to_char("You would want to block some direction, or none.\n\r", ch);

    return;
}

ACMD(do_specialize)
{
    extern const char* specialize_name[];

    if (GET_LEVEL(ch) < 12) {
        send_to_char("You are too young to specialize.\n\r", ch);
        return;
    }

    int num_of_specializations = (int)game_types::PS_Count;
    if (wtl && (wtl->targ1.type == TARGET_TEXT)) {
        game_types::player_specs current_spec = utils::get_specialization(*ch);
        if (current_spec != game_types::PS_None && current_spec != game_types::PS_Count) {
            sprintf(buf, "You are already specialized in %s.\n\r", specialize_name[current_spec]);
            send_to_char(buf, ch);
            return;
        }

        const char* spec_argument = argument + 1;

        int len = strlen(spec_argument);
        if (len == 0)
            return;

        for (int index = 1; index < num_of_specializations; index++) {
            // If the strings are equal.
            if (strncmp(specialize_name[index], spec_argument, len) == 0) {
                game_types::player_specs spec = game_types::player_specs(index);
                if (spec == game_types::PS_Count)
                    return;

                if (ch->player.race == RACE_ORC && spec == game_types::PS_Guardian) {
                    send_to_char("Snagas can't specialize in guardian!\n\r", ch);
                    return;
                }

                utils::set_specialization(*ch, spec);
                sprintf(buf, "You are now specialized in %s.\n\r", specialize_name[index]);
                send_to_char(buf, ch);

                // Some abilities may have been changed.  Recalc.
                recalc_abilities(ch);
                return;
            }
        }
    }

    int tmp = GET_SPEC(ch);
    if (tmp == 0 || (tmp >= num_of_specializations)) {
        strcpy(buf, "You can specialize in ");
        for (tmp = 1; tmp < num_of_specializations; tmp++) {
            strcat(buf, specialize_name[tmp]);
            strcat(buf, ", ");
        }

        buf[strlen(buf) - 2] = 0;
        strcat(buf, ".\n\r");
        send_to_char(buf, ch);
    } else {
        sprintf(buf, "You are specialized in %s.\n\r", specialize_name[tmp]);
        send_to_char(buf, ch);
        return;
    }
}

ACMD(do_fish)
{
    int fish = 7230;
    struct obj_data* tmpobj;
    struct room_data* cur_room;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    sh_int percent, move_use;
    percent = GET_SKILL(ch, SKILL_GATHER_FOOD);
    move_use = 25 - percent / 5;

    cur_room = &world[ch->in_room];

    fish = real_object(fish);
    tmpobj = read_object(fish, REAL);

    switch (subcmd) {
    case -1:
        abort_delay(ch);
        return;

    case 0:
        if (GET_MOVE(ch) < move_use) {
            send_to_char("You are too tired to fish right now.\n\r", ch);
            return;
        }

        if (cur_room->sector_type != SECT_WATER_NOSWIM) {
            send_to_char("You can't fish here.\n\r", ch);
            return;
        }

        if (!ch->equipment[HOLD] || !isname("fishing", ch->equipment[HOLD]->name)) {
            send_to_char("You need to hold a fishing pole to fish.\n\r", ch);
            return;
        }

        send_to_char("You cast your line into the water and begin to wait.\n\r", ch);

        WAIT_STATE_FULL(ch, MIN(20, 44 - GET_SKILL(ch, SKILL_GATHER_FOOD) / 5), CMD_FISH, 1, 30, 0,
            0, 0, AFF_WAITING | AFF_WAITWHEEL, TARGET_NONE);
        break;

    case 1:
        move_use = 25 - percent / 5;
        if (GET_MOVE(ch) < move_use) {
            send_to_char("You are too tired to fish right now.\n\r", ch);
            return;
        }

        GET_MOVE(ch) -= move_use;

        if (number(0, 115) > percent)
            send_to_char("You give up, having failed to catch anything.\n\r", ch);

        else if (number(0, 125) <= percent) {
            obj_to_char(tmpobj, ch);
            act("$n catches $p.", TRUE, ch, tmpobj, 0, TO_ROOM);
            act("You catch $p.", TRUE, ch, tmpobj, 0, TO_CHAR);
        }

        abort_delay(ch);
    }
}

struct {
    char* field;
    int subcmd;
} apply_options[] = { { "poison", SCMD_APPLY_POISON }, { "antidote", SCMD_APPLY_ANTIDOTE } };

const int num_of_apply = 2;

/*=================================================================================
   do_apply:
   This will apply a poison to a weapon or apply an antidote to a player if they
   are poisoned. The characters ranger level and gather skill determines how
   successful they are with apply the poison to the weapon, but there is no
   restriction on the antidote.
   ------------------------------Change Log---------------------------------------
   slyon: June 4, 2018 - Created ACMD
==================================================================================*/
ACMD(do_apply)
{
    struct char_data* vict;
    struct obj_data* tmp_object;
    int percent, bits, tmp;

    if (IS_NPC(ch)) {
        send_to_char("You're just a dumb NPC.\n\r", ch);
        return;
    }

    if (subcmd == 0) {
        if (!wtl || (wtl->targ1.type != TARGET_TEXT)) {
            send_to_char("Wrong call to apply. Consult an Immortal, please.\n\r", ch);
            return;
        }

        for (tmp = 0; tmp < num_of_apply; tmp++) {
            if (!str_cmp(apply_options[tmp].field, wtl->targ1.ptr.text->text))
                break;
        }

        if ((tmp == num_of_apply)) {
            strcpy(buf, "The format is 'apply <type> <object>', \n\rPossible fields are:\n\r");
            for (tmp = 0; tmp < num_of_apply; tmp++) {
                strcat(buf, apply_options[tmp].field);
                strcat(buf, ", ");
            }
            strcpy(buf + strlen(buf) - 2, ".\n\r");
            send_to_char(buf, ch);
            return;
        }

        subcmd = apply_options[tmp].subcmd;
    }

    switch (subcmd) {
    case SCMD_APPLY_POISON:
        break;
    case SCMD_APPLY_ANTIDOTE:
        break;
    default:
        break;
    }
}
