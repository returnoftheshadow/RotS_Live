/**************************************************************************
 *   File: spell_parser.c                                Part of CircleMUD *
 *  Usage: command interpreter for 'cast' command (spells)                 *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#include "platdef.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

#include "big_brother.h"
#include "char_utils.h"

extern struct descriptor_data* descriptor_list;
extern struct char_data* fast_update_list;
extern struct char_data* character_list;
extern struct char_data* waiting_list;
extern struct skill_data skills[];
extern struct room_data world;
extern char* spell_wear_off_msg[];
extern char* dirs[];

char* target_from_word(struct char_data*, char*, int, struct target_data*);
int check_hallucinate(struct char_data*, struct char_data*);
void report_wrong_target(struct char_data*, int, char);
void affect_update_person(struct char_data*, int);
char saves_spell(struct char_data*, sh_int, int);
bool new_saves_spell(const char_data* caster, const char_data* victim, int save_bonus);
void one_mobile_activity(struct char_data*);
void do_sense_magic(struct char_data*, int);
char saves_mystic(struct char_data*);
void appear(struct char_data*);
void affect_update();
void fast_update();
void check_break_prep(struct char_data* ch);

ACMD(do_flee);

void say_spell(char_data* caster, int spell_index)
{
    // Validity check.
    if (!caster || spell_index >= MAX_SKILLS)
        return;

    // Get the spell that we're casting.
    const skill_data& spell = skills[spell_index];
    const char* spell_name = spell.name;
    if (!spell_name) {
        log("Cast a spell without a name!  Unable to say the spell.");
        return;
    }

    // Reset the buffer.
    strcpy(buf, "");
    if (GET_RACE(caster) != RACE_HARADRIM) {
        sprintf(buf, "$n utters a strange command, '%s'", spell_name);
    } else {
        sprintf(buf, "$n utters a foreign command, '%s'", spell_name);
    }

    const room_data& room = world[caster->in_room];
    char_data* receiver = room.people;
    while (receiver) {
        if ((receiver != caster) && (GET_POS(receiver) > POSITION_SLEEPING)) {
            act(buf, FALSE, caster, 0, receiver, TO_VICT);
        }

        receiver = receiver->next_in_room;
    }
}

/*
 * For level 12 mages and higher, when magic is cast in the same
 * zone (though not the same room) by an opposite race, we send
 * them a 'sensing' message.
 */
void do_sense_magic(char_data* caster, int spell_number)
{
    const int MIN_MAGE_LEVEL_TO_SENSE = 12;

    if (skills[spell_number].type != PROF_MAGE || caster == NULL)
        return;

    for (descriptor_data* player = descriptor_list; player; player = player->next) {
        // Ignore disconnected players.
        if (player->connected == CON_PLYNG) {
            char_data* character = player->character;
            if (other_side(character, caster)) {
                // Players that are writing or asleep can't sense anything.
                if (!utils::is_player_flagged(*character, PLR_WRITING) && character->specials.position > POSITION_SLEEPING) {
                    if (utils::get_prof_level(PROF_MAGE, *character) >= MIN_MAGE_LEVEL_TO_SENSE) {
                        int caster_room = caster->in_room;
                        int character_room = character->in_room;

                        // Only send the message if characters are in different rooms within the
                        // same zone.
                        if (caster_room != character_room && world[caster_room].zone == world[character_room].zone) {
                            send_to_char("You sense a surge of unknown magic from nearby...\n\r",
                                character);
                        }
                    }
                }
            }
        }
    }
}

char saves_power(const char_data* victim, sh_int casting_power, sh_int save_bonus)
{
    sh_int victim_save_bonus = victim->points.willpower + save_bonus;

    int saving_throw_roll = number(0, victim_save_bonus * victim_save_bonus);
    int saving_throw_dc = number(0, casting_power * casting_power);

    if (saving_throw_roll > saving_throw_dc) {
        return 1;
    } else {
        return 0;
    }
}

/*
 * Saving a mage spell depends on many things (and mystic spells
 * should one day be changed so that they too depend on many
 * things).  First and foremost are the mage level of the caster,
 * passed to saves_spell as `level', and the amount of spell save
 * (`saving_throw' in char_special2_data) of the victim.  To make
 * mid-leveled mages less effective as offensive characters, we
 * apply a bonus of the minimum of: the level of `ch' and 30.  The
 * `bonus' argument is a bonus given to the VICTIM of the spell,
 * and is measured in raw spellsave points.  We then award a point
 * of spellsave for every 5 intelligence of the victim, and use a
 * little random generation to award a point of spellsave for
 * characters with intelligence not divisible by 5.  Finally, we
 * give a racial spellsave bonus to hobbits.
 *
 * Notes:
 * - The randomness of saving a spell ranges between 1 and 20, so
 *   1 point of spellsave is a 5% chance to save a spell.  Magical
 *   equipment should limit themselves to a general +1 (should they
 *   be considered magically resistant); +2 should be the upper
 *   limit for balanced magical save equipment, as +10% chance to
 *   save is quite a large advantage for one piece of equipment.
 * - There is still a bit of a problem when logging for spells like
 *   spear of darkness, which cannot be saved against.  The best
 *   solution to this is to probably make a spllog_* clobbering
 *   function (or macro, more likely) that is called by any spell
 *   that wishes to bypass saving a spell.
 */

/* These variables are used in fight.cc's damage() to aid in logging */
unsigned char spllog_saves; /* 1: character saved, 0: character failed */
short spllog_mage_level; /* the effective level of the caster */
short spllog_save; /* the effective save computed in saves_spell */

char saves_spell(struct char_data* ch, sh_int level, int bonus)
{
    int save;

    /* positive saving_throw makes saving throw better! */
    save = GET_SAVE(ch);
    save += GET_LEVELA(ch) - level;
    save += bonus;

    save += GET_INT(ch) / 5;
    if (GET_INT(ch) % 5)
        save += (number(0, GET_INT(ch) % 5)) ? 1 : 0;

    if (GET_RACE(ch) == RACE_HOBBIT)
        save += 1;

    spllog_mage_level = level;
    spllog_save = save;

    return (spllog_saves = (save > number(1, 20)));
}

//============================================================================
// Calculates the saving throw bonus of a character vs. Mage spells.
//============================================================================
int get_character_saving_throw(const char_data* victim)
{
    int saving_throw = 0;

    // NPCs are only considered 66% mages!  :D
    int level_bonus = utils::get_prof_level(PROF_MAGE, *victim);
    if (utils::is_npc(*victim)) {
        level_bonus = level_bonus * 2 / 3;
    }

    saving_throw += level_bonus / 3; // Add 1/3 level to save bonus, no rounding.
    saving_throw += (victim->tmpabilities.intel - 8) / 4;
    if (victim->player.race == RACE_HOBBIT) {
        saving_throw += 1;
    }

    return saving_throw;
}

//============================================================================
// Calculates the saving throw DC of a caster.
//   Spell_id is not currently used, but may be used in the future to make
//   it harder to save against spells from specialized mages.
//============================================================================
int get_saving_throw_dc(const char_data* caster)
{
    player_spec::battle_mage_handler battle_mage_handler(caster);
    int caster_dc = 10;
    caster_dc += utils::get_prof_level(PROF_MAGE, *caster) / 3;
    caster_dc += (caster->tmpabilities.intel - 8) / 4;
    return caster_dc + battle_mage_handler.get_bonus_spell_pen(caster->points.spell_pen);
}

//============================================================================
// Returns true if the victim saves against the spell, false otherwise.
//   Save bonus is added to the victim's base save value.
//============================================================================
bool new_saves_spell(const char_data* caster, const char_data* victim, int save_bonus)
{
    int save_value = get_character_saving_throw(victim) + save_bonus;
    int casting_dc = get_saving_throw_dc(caster);

    int roll = number(1, 20);
    bool saved = roll + save_value > casting_dc;

    spllog_mage_level = casting_dc;
    spllog_save = (short)save_value;
    spllog_saves = saved;

    // If these save bonus values are passed in, return a set result.
    // Do it down here so that the variables above can get set properly.
    if (save_bonus <= -20)
        return false;

    if (save_bonus >= 20)
        return true;

    return saved;
}

void record_spell_damage(struct char_data* caster, struct char_data* victim, int at, int dam)
{
    if (at == SPELL_CHILL_RAY || at == SPELL_LIGHTNING_BOLT || at == SPELL_DARK_BOLT || at == SPELL_CONE_OF_COLD || at == SPELL_FIREBOLT || at == SPELL_LIGHTNING_STRIKE || at == SPELL_FIREBALL || at == SPELL_WORD_OF_PAIN || at == SPELL_WORD_OF_AGONY || at == SPELL_SHOUT_OF_PAIN || at == SPELL_SPEAR_OF_DARKNESS || at == SPELL_LEACH || at == SPELL_BLACK_ARROW || at == SPELL_MAGIC_MISSILE || at == SPELL_EARTHQUAKE || at == SPELL_BLAZE || at == SPELL_SEARING_DARKNESS) {
        vmudlog(SPL, "spell=%s, damage=%d, from %s(%d) to %s(%d) %s", skills[at].name, dam,
            GET_NAME(caster), spllog_mage_level, GET_NAME(victim), spllog_save,
            spllog_saves ? "(saved)" : "");
        spllog_saves = 0;
    }
}

char char_perception_check(struct char_data* ch)
{
    int offense, defense;

    offense = number(0, 90);
    defense = GET_PERCEPTION(ch) + number(1, 20);

    return offense <= defense;
}

void check_break_prep(struct char_data* ch)
{
    ACMD(do_trap);

    if (ch->delay.cmd == CMD_PREPARE && (ch->delay.targ1.type == TARGET_IGNORE)) {
        send_to_char("Your preperations were ruined.\n\r", ch);
        ch->delay.targ1.cleanup();
        abort_delay(ch);
    } else if (ch->delay.cmd == CMD_TRAP) {
        act("Your carefully planned trap has been ruined.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n's carefully constructed trap is ruined!", FALSE, ch, 0, ch, TO_NOTVICT);
        do_trap(ch, "", NULL, CMD_TRAP, -1);
    }
}

/*
 * A pretty general save function that takes into consideration
 * perception.  Higher percep makes better save.
 */
char saves_mystic(struct char_data* ch)
{
    int offense, defense;

    offense = number(0, 100);
    defense = GET_PERCEPTION(ch) * 9 / 10;

    return offense <= defense;
}

/*
 * Saving poison depends on the caster's willpower and perception
 * and the victim's constitution, willpower and race.  As far as
 * race goes, wood elves simply get a bonus (a rather large one),
 * since they were very resilient to disease, but are represented
 * in rots by such low constitution.
 */
char saves_poison(struct char_data* victim, struct char_data* caster)
{
    int offence, defense;
    int perception = GET_PERCEPTION(caster);
    offence = ((GET_WILLPOWER(caster) * 8) * perception) / 100;
    /* wood elves get a bonus against poison */
    defense = (GET_CON(victim) * 5) + (GET_WILLPOWER(victim) * 3) + (GET_RACE(victim) == RACE_WOOD ? 30 : 0);

    return (number(offence / 3, offence) < number(defense / 2, defense));
}

/*
 * Saving confuse depends only on the willpowers of the caster
 * and the victim.  This should probably depend on some other
 * thing as well..
 */
char saves_confuse(struct char_data* victim, struct char_data* caster)
{
    int offence, defense;

    offence = GET_WILLPOWER(caster);
    defense = GET_WILLPOWER(victim) - (IS_NPC(victim) ? 5 : 0);

    return (number(0, offence) < number(0, defense));
}

/*
 * Saving insight is just about as awful as saving confuse; it
 * has pretty much nothing to do with anything other than the
 * victim and caster's willpower and perception.  Low perception
 * victims have terrible chances to save, as well.  This should
 * (like saves_confuse) one day take more things into account.
 */
char saves_insight(struct char_data* victim, struct char_data* caster)
{
    int offense, defense;

    offense = GET_PERCEPTION(caster) * GET_WILLPOWER(caster) / 100;
    defense = GET_PERCEPTION(victim) * GET_WILLPOWER(victim) / 100 + number(0, 10);

    return offense < defense;
}

/*
 * saves_leadership is used when checking to see if a victim
 * saves against a fear or terror spell.  Currently, saving
 * depends on the leadership skill (a skill only available to
 * common orcs) for following characters and players, and
 * ride skill for mounts.
 */
char saves_leadership(struct char_data* victim)
{
    int save;

    if (!(save = saves_mystic(victim)))
        if (victim->master)
            save = (number(1, 115) <= GET_SKILL(victim->master, SKILL_LEADERSHIP));
        else if (IS_RIDDEN(victim))
            save = (number(1, 100) <= GET_SKILL(victim->mount_data.rider, SKILL_RIDE));

    return save;
}

char* skip_spaces(char* string)
{
    for (; *string && (*string) == ' '; string++)
        ;

    return string;
}

namespace {
bool can_orc_follower_cast_spell(int spell_index)
{
    const int MAX_SPELLS = 17;

    // Orc followers cannot cast any "whitie" or "lhuth" only spells.
    // Assume any other spell is valid.
    // Exception:  Orcs can cast fire spells.
    static int invalid_spells[MAX_SPELLS] = {
        SPELL_CREATE_LIGHT, SPELL_DETECT_EVIL, SPELL_FLASH,
        SPELL_LIGHTNING_BOLT, SPELL_LIGHTNING_STRIKE, SPELL_WORD_OF_AGONY,
        SPELL_WORD_OF_PAIN, SPELL_WORD_OF_SHOCK, SPELL_BLACK_ARROW,
        SPELL_WORD_OF_SIGHT, SPELL_SPEAR_OF_DARKNESS, SPELL_LEACH,
        SPELL_SHOUT_OF_PAIN, SPELL_SANCTUARY, SPELL_CONFUSE,
        SPELL_PROTECTION, SPELL_GUARDIAN
    };

    for (int i = 0; i < MAX_SPELLS; ++i) {
        if (spell_index == invalid_spells[i]) {
            return false;
        }
    }

    return true;
}

bool is_spell_free(const int spell_index)
{
    switch (spell_index) {
    case SPELL_MASS_REGENERATION:
    case SPELL_MASS_INSIGHT:
    case SPELL_MASS_VITALITY:
    case SPELL_EXPOSE_ELEMENTS:
        return true;
    default:
        return false;
    }
}

bool can_cast_spell(char_data& character, int spell_index, const skill_data& spell)
{
    if (spell_index == SPELL_EXPOSE_ELEMENTS) {
        if (character.extra_specialization_data.is_mage_spec() == false) {
            send_to_char("You need to have a mage specialization to cast this spell!\n\r",
                &character);
            return false;
        }
    }

    if (utils::is_npc(character) && utils::is_mob_flagged(character, MOB_PET)) {
        // Ensure that the pet's master gets the message.
        char_data* message_recipient = character.master;
        if (character.master == NULL) {
            message_recipient = &character;
        }

        if (utils::is_mob_flagged(character, MOB_ORC_FRIEND)) {
            if (spell.level > character.player.level) {
                send_to_char("Pah!  Your follower is too weak for such a spell!\n\r",
                    message_recipient);
                return false;
            }

            if (spell.type == PROF_MAGE && character.get_cur_int() < 18) {
                send_to_char("Your stupid follower doesn't have the smarts for that!\n\r",
                    message_recipient);
                return false;
            } else if (spell.type == PROF_CLERIC && character.get_cur_wil() < 18) {
                send_to_char("Your dull follower doesn't have the will for that!\n\r",
                    message_recipient);
                return false;
            } else if (!can_orc_follower_cast_spell(spell_index)) {
                send_to_char("Blasphemy!  Orcs cannot utter such words...\n\r", message_recipient);
                return false;
            }
        } else {
            send_to_char("Since when can animals talk?\n\r", message_recipient);
            return false;
        }
    }

    int spell_prof = -1;
    /* Checking for the spell validity now */
    switch (skills[spell_index].type) {
    case PROF_MAGE:
        spell_prof = PROF_MAGE;
        break;
    case PROF_CLERIC:
        spell_prof = PROF_CLERIC;
        break;
    };

    /* checking specializations here */
    if (spell_prof == -1 || !spell.spell_pointer) {
        send_to_char("You can not cast this!!\n\r", &character);
        return false;
    }
    if ((GET_KNOWLEDGE(&character, spell_index) <= 0) && !is_spell_free(spell_index)) {
        send_to_char("You don't know this spell.\n\r", &character);
        return false;
    }
    if (GET_POS(&character) < spell.minimum_position) {
        send_to_char("You can't concentrate enough.\n\r", &character);
        return false;
    }

    if (spell.type == PROF_MAGE && (character.tmpabilities.mana < USE_MANA(&character, spell_index))) {
        send_to_char("You can't summon enough energy to cast the spell.\n\r", &character);
        return false;
    }
    if (spell.type == PROF_CLERIC && (character.points.spirit < USE_SPIRIT(&character, spell_index))) {
        send_to_char("You can't summon enough energy to cast the spell.\n\r", &character);
        return false;
    }

    /* Here checking that the character is allowed to cast the spell if they are
                in shadow form.  Probably a better way of doing this, but I can't think
                of it at the moment :) */

    // These checks spells seem like they are very particular.  Going into and out of shadow form?
    if (spell.min_usesmana == 55 && affected_by_spell(&character, SPELL_ANGER)) {
        send_to_char("You are too angry to cast this now.\n\r", &character);
        return false;
    }

    if (IS_SHADOW(&character) && spell.min_usesmana != 55) {
        send_to_char("You cannot cast this whilst dwelling in the shadow world.\n\r", &character);
        return false;
    }

    return true;
}

//============================================================================
// Returns the effective casting level for this caster and spell.
//============================================================================
double get_casting_level(const char_data* caster, int casting_level, int casting_stat,
    int spec_number)
{
    double final_level(casting_level);

    /* a bonus for anyone who is specialized in this spell's spec */
    if (utils::get_specialization(*caster) == spec_number) {
        final_level += (40.0 - final_level) * utils::get_level_legend_cap(*caster) / 150.0;
    }

    /* we give one level bonus for each 5 int */
    final_level += casting_stat / 5.0;
    return final_level;
}
} // namespace

/* Assumes that *argument does start with first letter of chopped string */
ACMD(do_cast)
{
    struct obj_data* tar_obj;
    struct char_data* tar_char;
    struct obj_data* tmpobj;
    int qend, i, tmp;
    int tar_dig;
    char* arg;
    int spell_prof, prepared_spell;
    int target_flag;
    struct waiting_type tmpwtl;
    int casting_time;

    int npc_can_cast_self = 0;
    int npc_self_spells[] = { SPELL_REGENERATION, SPELL_CURE_SELF, SPELL_CURING, SPELL_SHIELD };
    int nss_len = sizeof npc_self_spells / sizeof npc_self_spells[0];

    tmpwtl.targ1.type = tmpwtl.targ2.type = TARGET_NONE;
    player_spec::battle_mage_handler battle_mage_handler(ch);

    // could add handling here for base interrupt=3 stop casting mob handling,
    //    but should block mainly damage spells in combat (could consider letting powers try?)
    //    also if so, ignore this basic handling for: ch->specials.store_prog_number==31
    //      OR BETTER YET: ch->interrupt_handling=1 SET BY a prog that wants to handle itself

    if (subcmd == -1) {
        send_to_char("You could not concentrate anymore!\n\r", ch);

        if (utils::is_npc(*ch) && ch->interrupt_count < 3) {
            ch->interrupt_count = ch->interrupt_count + 1;
            if (ch->interrupt_time == 0) {
                ch->interrupt_time = 10;
            }
        }
        return;
    }

    if (IS_SET(world[ch->in_room].room_flags, PEACEROOM)) {
        send_to_char("Your lips falter and you cannot seem to find the words you seek.\n\r", ch);
        return;
    }

    /** no wtl, or wtl->subcmd==0  - the first call of do_cast,
                starting  to cast now **/

    if ((ch->delay.cmd == CMD_PREPARE) && (ch->delay.targ1.type == TARGET_IGNORE)) {
        prepared_spell = ch->delay.targ1.ch_num;
        ch->delay.subcmd = 2;
        complete_delay(ch);
    } else {
        prepared_spell = -1;
    }

    arg = argument;

    int spell_index = 0;
    if (!wtl || (wtl && !wtl->subcmd)) {
        /* is mob allowed to cast this spell on self? */
        for (int i = 0; i < nss_len; i++) {
            if (npc_self_spells[i] == wtl->targ1.ch_num) {
                npc_can_cast_self = 1;
                break;
            }
        }

        /* this takes the argument from the target parser */
        if (wtl && (wtl->targ1.type == TARGET_TEXT)) {
            arg = wtl->targ1.ptr.text->text;
            i = strlen(arg);

            /* which spell is it? */
            for (tmp = 0; tmp < MAX_SKILLS; tmp++)
                if (!strncmp(skills[tmp].name, arg, i))
                    break;

            if (tmp == MAX_SKILLS) {
                send_to_char("No such spell.\n\r", ch);
                return;
            }
            spell_index = tmp;

            // npc_can_cast_self
        } else if (wtl && (wtl->targ1.type == TARGET_OTHER || (wtl->targ1.type != TARGET_OTHER && npc_can_cast_self))) {
            spell_index = wtl->targ1.ch_num;
        } else { // wtl is no good, using the argument line.
            if (!argument) {
                printf("do_cast: no wtl, no argument\n");
                return;
            }

            arg = argument;
            arg = skip_spaces(arg);

            /* if there are no chars in argument */
            if (!(*arg)) {
                send_to_char("Cast which what where?\n\r", ch);
                return;
            }

            if (*arg != '\'') {
                send_to_char("Magic must always be enclosed by the holy magic symbols: '\n\r", ch);
                return;
            }

            /* Locate the last quote && lowercase the magic words (if any) */
            for (qend = 1; *(arg + qend) && (*(arg + qend) != '\''); qend++) {
                *(arg + qend) = LOWER(*(arg + qend));
            }

            if (*(arg + qend) != '\'') {
                send_to_char("Magic must always be enclosed by the holy magic symbols: '\n\r", ch);
                return;
            }

            for (tmp = 0; tmp < MAX_SKILLS; tmp++) {
                if (!strncmp(skills[tmp].name, arg + 1, qend - 1)) {
                    break;
                }
            }

            if (tmp == MAX_SKILLS) {
                send_to_char("No such spell.\n\r", ch);
                return;
            }
            spell_index = tmp;
        }

        const skill_data& spell = skills[spell_index];
        /* Checking for the spell validity now */
        switch (spell.type) {
        case PROF_MAGE:
            spell_prof = PROF_MAGE;
            break;
        case PROF_CLERIC:
            spell_prof = PROF_CLERIC;
            break;
        default:
            spell_prof = -1;
            break;
        };

        if (!can_cast_spell(*ch, spell_index, spell))
            return;

        tmpwtl.targ1.type = TARGET_OTHER;
        tmpwtl.targ1.ch_num = spell_index;
        /* Okay, the spell is selected, now to the target */

        if (wtl && (wtl->targ2.choice & skills[spell_index].targets)) {
            tmpwtl.targ2 = wtl->targ2;
        } else {
            if (wtl && (wtl->targ2.type == TARGET_TEXT)) {
                arg = wtl->targ2.ptr.text->text;
            }

            /* else we have arg from the above spell search */
            if (!target_from_word(ch, arg, skills[spell_index].targets, &tmpwtl.targ2)) {
                report_wrong_target(ch, skills[spell_index].targets, (*arg) ? 1 : 0);
                return;
            }

            // The spell is targeting a character.  Ensure that it's valid before continuing.
            if (tmpwtl.targ2.type == TARGET_CHAR && tmpwtl.targ2.ptr.ch) {
                game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
                if (!bb_instance.is_target_valid(ch, tmpwtl.targ2.ptr.ch, spell_index)) {
                    send_to_char("You feel the Gods looking down upon you, and protecting your "
                                 "target.  Your lips falter.\r\n",
                        ch);
                    return;
                }

                if (spell_index == SPELL_EXPOSE_ELEMENTS) {
                    if (utils::is_pc(*tmpwtl.targ2.ptr.ch)) {
                        send_to_char("You cannot target players with that spell.\r\n", ch);
                        return;
                    } else if (ch->extra_specialization_data.is_mage_spec()) {
                        elemental_spec_data* spec_data = ch->extra_specialization_data.get_mage_spec();
                        if (spec_data->exposed_target == tmpwtl.targ2.ptr.ch) {
                            send_to_char(
                                "You have already exposed your target to the elements!\n\r", ch);
                            return;
                        }
                    }
                }
            }
        }
        /* supposedly, we have ch.delay formed now, except for delay value. */

        // only allow NPCs and god race to cast in rooms (for now)
        auto restricted_room_haze_cast = spell_index == SPELL_HAZE && tmpwtl.targ2.type == 0 && !ch->specials.fighting && !(IS_NPC(ch) || GET_RACE(ch) == 0);

        if (restricted_room_haze_cast) {
            send_to_char("You cannot cast to room.\n\r", ch);
            return;
        }

        if (!(prepared_spell == spell_index) && !IS_SET(ch->specials.affected_by, AFF_WAITING)) {
            /* putting the player into waiting list */
            casting_time = CASTING_TIME(ch, spell_index);
            if (spell_prof == PROF_MAGE && spell_index != SPELL_EXPOSE_ELEMENTS) {
                if (GET_CASTING(ch) == CASTING_FAST) {
                    casting_time = std::max(1, (casting_time + 1) / 2);
                } else if (GET_CASTING(ch) == CASTING_SLOW) {
                    casting_time = std::max(1, (casting_time * 3 + 1) / 2);
                }
            }

            WAIT_STATE_BRIEF(ch, casting_time, cmd, spell_index, 30, AFF_WAITING | AFF_WAITWHEEL);
            ch->delay.targ1 = tmpwtl.targ1;
            ch->delay.targ2 = tmpwtl.targ2;
            tmpwtl.targ1.cleanup();
            tmpwtl.targ2.cleanup();
            act("$n begins quietly muttering some strange, powerful words.\n\r", FALSE, ch, 0, 0,
                TO_ROOM);
            send_to_char("You start to concentrate.\n\r", ch);
            return; /* time delay set, returning */
        } else {
            ch->delay.cmd = cmd;
            ch->delay.subcmd = spell_index;
            ch->delay.targ1 = tmpwtl.targ1;
            ch->delay.targ2 = tmpwtl.targ2;
            tmpwtl.targ1.cleanup();
            tmpwtl.targ2.cleanup();
            wtl = &ch->delay;
        }
    }

    /* ok, now the caster has waited his respective time, and
     * we're going to actually cast the spell */
    REMOVE_BIT(ch->specials.affected_by, AFF_WAITING);
    REMOVE_BIT(ch->specials.affected_by, AFF_WAITWHEEL);

    tar_char = 0;
    tar_obj = 0;
    tar_dig = 0;
    spell_index = wtl->subcmd;
    target_flag = wtl->targ2.choice;

    if (wtl->subcmd == -1)
        return;

    if (IS_SET(target_flag, TAR_CHAR_ROOM | TAR_CHAR_WORLD | TAR_FIGHT_VICT)) {
        tar_char = wtl->targ2.ptr.ch;

        // The spell is targeting a character.  Ensure that it's valid before continuing.
        game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
        if (!bb_instance.is_target_valid(ch, tar_char, spell_index)) {
            send_to_char("You feel the Gods looking down upon you, and protecting your target.  "
                         "Your lips falter.\r\n",
                ch);
            return;
        }

        /* get rid of sanctuaries for any spell targetted on someone other
         * than themseles */
        if (tar_char && (tar_char != ch) && IS_AFFECTED(ch, AFF_SANCTUARY)) {
            appear(ch);
            send_to_char("You cast off your sanctuary!\n\r", ch);
            act("$n renouces $s sanctuary!", FALSE, ch, 0, 0, TO_ROOM);
        }
    }

    if (IS_SET(target_flag, TAR_OBJ_INV | TAR_OBJ_ROOM | TAR_OBJ_WORLD | TAR_OBJ_EQUIP)) {
        tar_obj = wtl->targ2.ptr.obj;
    }

    if (IS_SET(target_flag, TAR_TEXT | TAR_TEXT_ALL)) {
        arg = wtl->targ2.ptr.text->text;
    }

    // This switch statement determines if the spell-casting is still valid.
    switch (target_flag) {
    case TAR_DIR_NAME:
    case TAR_DIR_WAY: {
        tar_dig = wtl->targ2.ch_num;
        if (tar_dig < 0 || tar_dig > NUM_OF_DIRS) {
            send_to_char("Error in direction spell, please notify imps.\n\r", ch);
            return;
        }
    } break;
    case TAR_CHAR_ROOM: {
        if (tar_char->in_room != ch->in_room) {
            send_to_char("Your victim has fled.\n\r", ch);
            return;
        }
    } break;
    case TAR_CHAR_WORLD: // supposedly he's still somewhere around :-)
        break;
    case TAR_OBJ_INV: {
        // Find the object that we're targeting.
        for (tmpobj = ch->carrying; tmpobj; tmpobj = tmpobj->next_content) {
            if (tmpobj == tar_obj)
                break;
        }

        if (!tmpobj) {
            send_to_char("Your target disappeared.\n\r", ch);
            return;
        }
    } break;
    case TAR_OBJ_ROOM: {
        if (tar_obj->in_room != ch->in_room) {
            send_to_char("Your target disappeared.\n\r", ch);
            return;
        }
    } break;
    case TAR_OBJ_WORLD: // again, where could it possibly go...
        break;
    case TAR_OBJ_EQUIP: {
        for (tmp = 0; tmp < MAX_WEAR; tmp++) {
            if (ch->equipment[tmp] == tar_obj)
                break;
        }
        if (tmp == MAX_WEAR) {
            send_to_char("Your target disappeared.\n\r", ch);
            return;
        }
    } break;
    case TAR_SELF_ONLY:
    case TAR_SELF:
        tar_char = ch;
        break;
    case TAR_FIGHT_VICT: {
        if ((tar_char != ch->specials.fighting) || (tar_char->in_room != ch->in_room)) {
            send_to_char("You could not find your opponent.\n\r", ch);
            return;
        }
    } break;
    case TAR_IGNORE:
    case TAR_NONE_OK: {
        tar_char = 0;
        tar_obj = 0;
    } break;
    case TAR_TEXT:
    case TAR_TEXT_ALL:
        break;
    default:
        send_to_char("Unknown target option, please notify imps.\n\r", ch);
        return;
    }

    say_spell(ch, spell_index);
    do_sense_magic(ch, spell_index);
    if ((skills[spell_index].spell_pointer == 0) && spell_index >= 0) {
        send_to_char("Sorry, this magic has not yet been implemented :(\n\r", ch);
    } else {
        if (IS_NPC(ch)) {
            // Orc followers are considered to have 100% knowledge of all spells.
            if (MOB_FLAGGED(ch, MOB_ORC_FRIEND) && ch->master) {
                tmp = 100;
            } else {
                tmp = ch->player.level * 8 - skills[spell_index].level * 10;
                tmp = std::max(tmp, 0);
                tmp = tmp + 25;
            }
        } else {
            tmp = GET_KNOWLEDGE(ch, spell_index);

            // Characters that can cast 'Expose Elements' are considered 100% trained in it.
            if (is_spell_free(spell_index)) {
                tmp = 100;
            }
        }

        /* encumberance spell penalty, about 10% at max. encumberance */
        if (skills[spell_index].type == PROF_MAGE && battle_mage_handler.does_armor_fail_spell()) {
            tmp -= utils::get_encumbrance(*ch) / 3 - 1;
        }

        tmp -= get_power_of_arda(ch);

        tmp = std::min(tmp, 100);

        if (number(0, 100) >= tmp) {
            send_to_char("You lost your concentration!\n\r", ch);
            if (skills[spell_index].type == PROF_MAGE) {
                GET_MANA(ch) -= (USE_MANA(ch, spell_index) >> 1);
            } else {
                utils::add_spirits(ch, -USE_SPIRIT(ch, spell_index) >> 1);
            }

            return;
        }

        if (skills[spell_index].type == PROF_MAGE) {
            int mana_cost = USE_MANA(ch, spell_index);
            if (spell_index != SPELL_EXPOSE_ELEMENTS) {
                int casting = GET_CASTING(ch);
                if (casting == CASTING_FAST) {
                    mana_cost = mana_cost * 3 / 2;
                } else if (casting == CASTING_SLOW) {
                    mana_cost = (mana_cost * 1 + 1) / 2;
                }
            }

            elemental_spec_data* spec_data = ch->extra_specialization_data.get_mage_spec();
            if (spec_data) {
                if (tar_char == spec_data->exposed_target) {
                    // Currently, spells cast by expose elements are free.
                    if (spell_index == spec_data->spell_id) {
                        mana_cost = 0;
                        if (GET_CASTING(ch) == CASTING_FAST) {
                            mana_cost = USE_MANA(ch, spell_index) / 2;
                        } else if (GET_CASTING(ch) == CASTING_SLOW) {
                            // Casting slow with expose elements restores mana.
                            mana_cost = -USE_MANA(ch, spell_index) / 3;
                        }
                    }
                }
            }

            GET_MANA(ch) -= mana_cost;
        }
        /* it's a cleric spell */
        else {
            int spirit_cost = USE_SPIRIT(ch, spell_index);
            affected_type* aff = affected_by_spell(ch, SPELL_FAME_WAR);
            if (aff && utils::get_highest_coeffs(*ch) == PROF_CLERIC) {
                spirit_cost = spirit_cost * 0.80;
            }
            utils::add_spirits(ch, -spirit_cost);
        }

        send_to_char("Ok.\n\r", ch);

        /*
         * failing to hallucinate means no spell is cast, but this
         * is only the behavior assuming that there IS a target, and
         * that the target is not yourself.
         */
        if (tar_char && tar_char != ch && !check_hallucinate(ch, tar_char)) {
            return;
        }

        /* execute the spell */
        ((*skills[spell_index].spell_pointer)(ch, arg, SPELL_TYPE_SPELL, tar_char, tar_obj, tar_dig,
            0));

        /*
         * Casting a prepared spell now causes a short after-spell
         * lag.  Why do we use beats / 4?  A 30m's fireball (which
         * is the longest lag spell I can think of - unless spear
         * is longer) has a time of 15: thus beats/4 is basically
         * *never* greater than zero.
         */
        if (prepared_spell == spell_index) {
            WAIT_STATE_BRIEF(ch, number(1, skills[spell_index].beats / 4), -1, 0, 50, AFF_WAITING);
        }

        wtl->targ1.cleanup();
        wtl->targ2.cleanup();
    }
    return;
}

/*
 * Prepare used to fail unconditionally if the caster (ch)
 * was confused.  This code has been commented out for a long
 * time, and I just removed it.  It, however, might not be
 * that awful of an idea.  Another bit of commented-out code
 * that I removed caused prepare to fail should the caster
 * not have enough mana (or a cleric not have enough spirit)
 * to cast the spell at prepare time.
 *
 * Subcommmands:
 *   0 - Initial call to prepare, cause delay and store in a
 *       temporary structure what is being prepared.
 *   1 - After the prepare delay, causes the prepared spell to
 *       be stored.
 */
ACMD(do_prepare)
{
    char* arg;
    int i, tmp, spl, qend, spell_prof;
    void abort_delay(struct char_data*);
    player_spec::battle_mage_handler battle_mage_handler(ch);
    if (!battle_mage_handler.can_prepare_spell()) {
        send_to_char("Battle mages can't prepare spells.\n\r", ch);
        return;
    }

    if (subcmd == -1) {
        send_to_char("Your preparations were ruined.\n\r", ch);
        ch->delay.targ1.cleanup();
        abort_delay(ch);
        return;
    }

    if (subcmd == 0) {
        if (!wtl || (wtl && !wtl->subcmd)) {
            if (wtl && wtl->targ1.type == TARGET_TEXT) {
                arg = wtl->targ1.ptr.text->text;
                i = strlen(arg);
                for (tmp = 0; tmp < MAX_SKILLS; tmp++)
                    if (!strncmp(skills[tmp].name, arg, i))
                        break;

                if (tmp == MAX_SKILLS) {
                    send_to_char("No such spell.\n\r", ch);
                    return;
                }
                spl = tmp;

            } else if (wtl && (wtl->targ1.type == TARGET_OTHER))
                spl = wtl->targ1.ch_num;
            else { /* wtl isn't useful, use the command line */
                if (!argument)
                    return;

                arg = argument;
                arg = skip_spaces(arg);

                /* If there is no chars in argument */
                if (!(*arg)) {
                    send_to_char("Prepare what?\n\r", ch);
                    return;
                }

                if (*arg != '\'') {
                    send_to_char("Magic must always be enclosed by the holy "
                                 "magic symbols: '\n\r",
                        ch);
                    return;
                }

                /* Locate the last quote && lowercase the magic words (if any) */
                for (qend = 1; *(arg + qend) && (*(arg + qend) != '\''); qend++)
                    *(arg + qend) = LOWER(*(arg + qend));

                if (*(arg + qend) != '\'') {
                    send_to_char("Magic must always be enclosed by the holy "
                                 "magic symbols: '\n\r",
                        ch);
                    return;
                }

                for (tmp = 0; tmp < MAX_SKILLS; tmp++)
                    if (!strncmp(skills[tmp].name, arg + 1, qend - 1))
                        break;

                if (tmp == MAX_SKILLS) {
                    send_to_char("No such spell.\n\r", ch);
                    return;
                }
                spl = tmp;
            }

            /** Checking for the spell validity now **/
            switch (skills[spl].type) {
            case PROF_MAGE:
                spell_prof = PROF_MAGE;
                break;
            case PROF_CLERIC:
                spell_prof = PROF_CLERIC;
                break;
            default:
                spell_prof = -1;
                break;
            }

            if (spell_prof == -1 || !skills[spl].spell_pointer) {
                send_to_char("You can not cast this!\n\r", ch);
                return;
            }

            if (spell_prof == PROF_CLERIC) {
                send_to_char("You can not prepare this in advance.\n\r", ch);
                return;
            }

            if (GET_KNOWLEDGE(ch, spl) <= 0) {
                send_to_char("You don't know this spell.\n\r", ch);
                return;
            }

            WAIT_STATE_BRIEF(ch, CASTING_TIME(ch, spl) * 2, cmd, 1, 30,
                AFF_WAITING | AFF_WAITWHEEL);

            ch->delay.targ1.type = TARGET_OTHER;
            ch->delay.targ1.ch_num = spl;

            act("$n begins some strange preparations.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("You start to prepare your spell.\n\r", ch);
            return; /*    !! time delay set, returning !! */
        } else {
            send_to_char("You're busy already.\n\r", ch);
            return;
        }
    }

    if (subcmd == 1) {
        if (ch->delay.targ1.type != TARGET_OTHER)
            return;

        if (GET_KNOWLEDGE(ch, ch->delay.targ1.ch_num) < number(1, 120)) {
            send_to_char("You fumbled with your preparations.\n\r", ch);
            ch->delay.targ1.cleanup();
            return;
        }

        spl = ch->delay.targ1.ch_num;
        WAIT_STATE_BRIEF(ch, -1, CMD_PREPARE, spl, 20, 0);
        send_to_char("You completed your preparations.\n\r", ch);
        return;
    }
    if (subcmd == 2)
        send_to_char("You release your prepared spell.\n\r", ch);
}
