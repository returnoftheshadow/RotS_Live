/* ************************************************************************
 *   File: mystic.cc                                     Part of CircleMUD *
 *   Usage: actual effects of mystical spells                              *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

/*
 * Mystic.cc is part of a code clean up project
 * Mystic.cc is derived from circlemuds magic.cc
 * The change over was made in an effort to better organise
 * the code base by the rots-devel team (go us)
 * Change made by Khronos 28/03/05
 */

#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "platdef.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <iostream>
#include <sstream>

/*---------------------------------------------------------------------------------------------*/

/*
 * Enternal Structures
 */

extern struct room_data world;
extern struct obj_data* obj_proto;
extern struct char_data* character_list;
extern int top_of_world;
extern int rev_dir[];
extern char* dirs[];
extern char* room_bits[];
extern char* sector_types[];
extern int guardian_mob[][3];
extern char* race_abbrevs[];
/*
 *External functions
 */

char saves_mystic(struct char_data*);
char saves_poison(struct char_data*, struct char_data*);
char saves_confuse(struct char_data*, char_data*);
char saves_leadership(struct char_data*);
char saves_insight(struct char_data*, struct char_data*);
bool check_mind_block(char_data*, char_data*, int, int);
void list_char_to_char(struct char_data* list, struct char_data* caster,
    int mode);
ACMD(do_look);

int get_mystic_caster_level(const char_data* caster)
{
    int mystic_level = utils::get_prof_level(PROF_CLERIC, *caster);

    // Factor in will values not divisible by 5.
    int will_factor = caster->tmpabilities.wil / 5;
    if (number(0, will_factor % 5) > 0) {
        ++will_factor;
    }

    return mystic_level + will_factor;
}

/*
 * Use this macro to cause objects to override any affections
 * already on a character.  For example, the onyx ring's evasion
 * affection deletes an previous evasion and replaces it with its
 * permanent evasion.
 */
#define OBJECT_OVERRIDE_AFFECTIONS(_type, _isobj, _ch, _spl) \
    do {                                                     \
        if ((_isobj)) {                                      \
            if ((_type) == SPELL_TYPE_ANTI) {                \
                affect_from_char_notify((_ch), (_spl));      \
                return;                                      \
            } else                                           \
                affect_from_char((_ch), (_spl));             \
        }                                                    \
    } while (0)

/*
 * Mystic spells are order by category and level
 * The spells are split into 4 Categories
 *  - Perception and Mental Skills
 *  - Self_Affection Spells
 *  - Regeneration Spells
 *  - Offensive (for lack of a better word) skills
 *  - Everything Else
 */

/* Perception and Mental skills ordered by level
 * - Curse
 * - Revive
 * - Mind Block
 * - Insight
 * - Pragmatism
 */

ACMD(do_flee);
combat_result_struct damage_stat(char_data* killer, char_data* caster, int stat_num, int amount);

ASPELL(spell_curse)
{
    const int DAMAGE_TABLE_SIZE = 7;
    const int NUM_STATS = 6;

    if (GET_MENTAL_DELAY(caster) > PULSE_MENTAL_FIGHT + 1) {
        send_to_char("Your mind is not ready yet.\n\r", caster);
        return;
    }

    int level = get_mystic_caster_level(caster);
    int count = (level + 2 * 10) * GET_PERCEPTION(victim) / 100 / 10;
    if (!count) {
        act("You try to curse $N, but can't reach $S mind.", FALSE, caster, 0, victim, TO_CHAR);
        return;
    }

    if (affected_by_spell(caster, SPELL_MIND_BLOCK)) {
        act("You cannot curse with a blocked mind.", FALSE, caster, 0, victim, TO_CHAR);
        return;
    }

    if (GET_BODYTYPE(victim) != 1 && !IS_SHADOW(victim)) {
        act("You try to curse $N, but could not fathom its mind.", FALSE, caster, 0, victim, TO_CHAR);
        return;
    }

    int damage_table[DAMAGE_TABLE_SIZE];
    for (int stat_index = 0; stat_index < DAMAGE_TABLE_SIZE; stat_index++) {
        damage_table[stat_index] = 0;
    }

    /* choosing the first stat to damage */
    int num = number(0, NUM_STATS);
    int last_stat = num;

    damage_table[num] = 1;
    count--;

    for (int stat_index = 0; stat_index < count; stat_index++) {
        num = number(0, 9);
        if (num > NUM_STATS) {
            num = last_stat;
        } else {
            last_stat = num;
        }

        damage_table[num]++;
    }

    act("$n points ominously at $N and curses.", TRUE, caster, 0, victim, TO_NOTVICT);
    act("$n points ominously at you and curses.", TRUE, caster, 0, victim, TO_VICT);
    act("You point at $N and curse.", FALSE, caster, 0, victim, TO_CHAR);

    bool victim_flees = false;
    bool victim_died = false;

    int actual_count = 0;
    int stat_index = 0;

    while (stat_index < NUM_STATS && !victim_died && caster->points.spirit > 0) {
        int stat_damage = damage_table[stat_index];
        if (stat_damage > 0) {
            caster->points.spirit -= number(0, stat_damage);
            caster->points.spirit = std::max(caster->points.spirit, 0);

            combat_result_struct curse_result = damage_stat(caster, victim, stat_index, stat_damage);
            victim_flees |= curse_result.wants_to_flee;
            victim_died |= curse_result.will_die;

            actual_count += stat_damage;
        }

        ++stat_index;
    }

    // Only flee after the curse has completed attacking all of its stats.
    if (!victim_died && victim_flees) {
        do_flee(victim, "", NULL, 0, 0);
    }

    set_mental_delay(caster, actual_count * PULSE_MENTAL_FIGHT);
}

int restore_stat(char_data* caster, int stat_num, int amount);

ASPELL(spell_revive)
{
    int i, i2, num, count, actual_count, stat_dam, stat_dam2;
    int revive_table[7];

    if (GET_MENTAL_DELAY(caster) > PULSE_MENTAL_FIGHT + 1) {
        send_to_char("Your mind is not ready yet.\n\r", caster);
        return;
    }

    int level = get_mystic_caster_level(caster);
    count = (3 * 9 + level) * GET_PERCEPTION(victim) / 100 / 9;

    if (count * 2 > utils::get_spirits(caster)) {
        count = utils::get_spirits(caster) / 2;
    }

    if (count <= 0) {
        send_to_char("You couldn't gather enough spirit to heal.\n\r", caster);
        return;
    }

    stat_dam = 100;
    i = -1;

    stat_dam2 = GET_STR(victim) / GET_STR_BASE(victim);
    if (stat_dam2 < stat_dam) {
        stat_dam = stat_dam2;
        i = 0;
    }
    stat_dam2 = GET_INT(victim) / GET_INT_BASE(victim);
    if (stat_dam2 < stat_dam) {
        stat_dam = stat_dam2;
        i = 1;
    }
    stat_dam2 = GET_WILL(victim) / GET_WILL_BASE(victim);
    if (stat_dam2 < stat_dam) {
        stat_dam = stat_dam2;
        i = 2;
    }
    stat_dam2 = GET_DEX(victim) / GET_DEX_BASE(victim);
    if (stat_dam2 < stat_dam) {
        stat_dam = stat_dam2;
        i = 3;
    }
    stat_dam2 = GET_CON(victim) / GET_CON_BASE(victim);
    if (stat_dam2 < stat_dam) {
        stat_dam = stat_dam2;
        i = 4;
    }
    stat_dam2 = GET_LEA(victim) / GET_LEA_BASE(victim);
    if (stat_dam2 < stat_dam) {
        stat_dam = stat_dam2;
        i = 5;
    }

    if (i < 0) {
        send_to_char("No healing was needed there.\n\r", caster);
        return;
    }
    num = i;

    for (i = 0; i < 7; i++)
        revive_table[i] = 0;

    for (i = 0; i < count; i++) {
        i2 = number(0, 10);
        if (i2 <= 6)
            num = i2;
        revive_table[num]++;
    }
    act("$n tries to revive $N.", FALSE, caster, 0, victim, TO_NOTVICT);
    act("$n tries to revive you.", FALSE, caster, 0, victim, TO_VICT);
    act("You try to revive $N.", TRUE, caster, 0, victim, TO_CHAR);

    actual_count = 0;
    for (i = 0; i < 6; i++) {
        num = restore_stat(victim, i, revive_table[i]);
        actual_count += num;
        utils::add_spirits(caster, -num);
        if (utils::get_spirits(caster) <= 0)
            break;
    }
    if (!actual_count) {
        act("Your spell does no good to $M.", FALSE, caster, 0, victim, TO_CHAR);
        act("$n tries to revive you, but does little good.", FALSE, caster, 0,
            victim, TO_VICT);
    } else {
        set_mental_delay(caster, actual_count * PULSE_MENTAL_FIGHT);
    }
}

ASPELL(spell_mind_block)
{
    struct affected_type af;

    if (victim != caster) {
        send_to_char("You can only protect your own mind.\n\r", caster);
        return;
    }
    if (affected_by_spell(caster, SPELL_MIND_BLOCK)) {
        send_to_char("Your mind is protected already.\n\r", caster);
        return;
    }
    af.type = SPELL_MIND_BLOCK;
    af.duration = 15 + GET_PROF_LEVEL(PROF_CLERIC, caster) * 2;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = 0;

    affect_to_char(caster, &af);
    send_to_char("You create a magical barrier around your mind.\n\r", victim);
}

ASPELL(spell_insight)
{
    affected_type af;
    affected_type* afptr;
    int my_duration;

    if ((type == SPELL_TYPE_ANTI) || is_object) {

        affect_from_char(victim, SPELL_INSIGHT);

        if (type == SPELL_TYPE_ANTI)
            return;
    }

    if ((victim != caster) && !is_object) {
        // if(GET_PERCEPTION(victim)*2 < number(0,100)){
        if (saves_insight(victim, caster)) {
            act("You failed to affect $S mind.", FALSE, caster, 0, victim, TO_CHAR);
            return;
        }
    }

    int level = get_mystic_caster_level(caster);
    if (is_object)
        my_duration = -1;
    else
        my_duration = 10 + level;

    if ((afptr = affected_by_spell(victim, SPELL_PRAGMATISM))) {
        if (!is_object && (number(0, afptr->duration) > number(0, my_duration))) {
            act("You failed to break $S pragmatism.", FALSE, caster, 0, victim, TO_CHAR);
            return;
        } else {
            act("You break $S pragmatism.", FALSE, caster, 0, victim, TO_CHAR);
            affect_from_char(victim, SPELL_PRAGMATISM);
        }
    }

    if (!affected_by_spell(victim, SPELL_INSIGHT)) {
        af.type = SPELL_INSIGHT;
        af.duration = my_duration;
        af.modifier = 50;
        af.location = APPLY_PERCEPTION;
        af.bitvector = 0;

        affect_to_char(victim, &af);
        send_to_char("The world seems to gain a few edges for you.\n\r", victim);
    } else {
        act("$E has insight already.", FALSE, caster, 0, victim, TO_CHAR);
        return;
    }
}

ASPELL(spell_pragmatism)
{
    affected_type af;
    affected_type* afptr;
    int my_duration;

    if (victim != caster) {
        if (GET_PERCEPTION(victim) < number(0, 100)) {
            act("You failed to affect $S mind.", FALSE, caster, 0, victim, TO_CHAR);
            return;
        }
    }

    int level = get_mystic_caster_level(caster);

    if (is_object)
        my_duration = -1;
    else
        my_duration = 10 + level;

    if ((afptr = affected_by_spell(victim, SPELL_INSIGHT))) {
        if (number(0, afptr->duration) > number(0, my_duration)) {
            act("You failed to break $S insight.", FALSE, caster, 0, victim, TO_CHAR);
            return;
        } else {
            act("You break $S insight.", FALSE, caster, 0, victim, TO_CHAR);
            affect_from_char(victim, SPELL_INSIGHT);
        }
    }

    if (!affected_by_spell(victim, SPELL_PRAGMATISM)) {
        af.type = SPELL_PRAGMATISM;
        af.duration = 10 + level;
        if (GET_RACE(victim) != RACE_WOOD)
            af.modifier = -50;
        else
            af.modifier = -100;
        af.location = APPLY_PERCEPTION;
        af.bitvector = 0;

        affect_to_char(victim, &af);
        send_to_char("The world seems much duller..\n\r", victim);
    } else {
        act("$E is quite pragmatic already.", FALSE, caster, 0, victim, TO_CHAR);
        return;
    }
}

/*
 * Self Affection spells listed below in order
 * - Detect Hidden
 * - Detect Magic
 * - Evasion
 * - Resist Magic
 * - Slow Digestion
 * - Divination
 * - Infravision
 */

ASPELL(spell_detect_hidden)
{
    struct affected_type af;
    int loc_level, my_duration;

    if (!victim)
        return;

    if ((type == SPELL_TYPE_ANTI) || is_object) {
        affect_from_char(victim, SPELL_DETECT_HIDDEN);
        if (type == SPELL_TYPE_ANTI)
            return;
    }

    int level = get_mystic_caster_level(caster);
    if (victim != caster)
        loc_level = GET_PROF_LEVEL(PROF_CLERIC, victim) + level;
    else
        loc_level = level;

    if (is_object)
        my_duration = -1;
    else
        my_duration = 3 * loc_level;

    if (!affected_by_spell(victim, SPELL_DETECT_HIDDEN)) {
        send_to_char("You feel your awareness improve.\n\r", victim);

        af.type = SPELL_DETECT_HIDDEN;
        af.duration = my_duration;
        af.modifier = 0;
        af.location = APPLY_NONE;
        af.bitvector = AFF_DETECT_HIDDEN;
        affect_to_char(victim, &af);
    }
}

ASPELL(spell_detect_magic)
{
    struct affected_type af;

    if (!victim)
        victim = caster;

    if (affected_by_spell(victim, SPELL_DETECT_MAGIC)) {
        if (victim == caster)
            send_to_char("You already can sense magic.\n\r", caster);
        else
            act("$E already can sense magic.\n\r", TRUE, caster, 0, victim, TO_CHAR);
        return;
    }

    int level = get_mystic_caster_level(caster);
    af.type = SPELL_DETECT_MAGIC;
    af.duration = level * 5;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = AFF_DETECT_MAGIC;

    affect_to_char(victim, &af);
    send_to_char("Your eyes tingle.\n\r", victim);
}

ASPELL(spell_evasion)
{
    struct affected_type af;
    int loc_level, my_duration;

    if (!victim)
        return;

    if ((type == SPELL_TYPE_ANTI) || is_object) {
        affect_from_char(victim, SPELL_ARMOR);
        if (type == SPELL_TYPE_ANTI)
            return;
    }

    int level = get_mystic_caster_level(caster);
    if (victim != caster)
        loc_level = (GET_PROF_LEVEL(PROF_CLERIC, victim) + level + 5) / 4;
    else
        loc_level = (level + 5) / 2;

    if (is_object)
        my_duration = -1;
    else
        my_duration = 12 + loc_level;

    if (!affected_by_spell(victim, SPELL_ARMOR)) {
        af.type = SPELL_ARMOR;
        af.duration = my_duration;
        af.modifier = loc_level;
        af.location = APPLY_ARMOR;
        af.bitvector = AFF_EVASION;

        affect_to_char(victim, &af);
        send_to_char("You feel someone protecting you.\n\r", victim);
    }
}

ASPELL(spell_resist_magic)
{
    if (!victim)
        return;

    // drelidan: New formula.  +1 save per 3 mage levels.  With resist magic
    // up, add half of your cleric levels to your mage levels first.
    int level = get_mystic_caster_level(caster);
    int modifier = level / 6;

    // Protection specialization gets additional defenses against magic.
    if (utils::get_specialization(*caster) == game_types::PS_Protection) {
        modifier += 1;
    }

    affected_type af;
    af.type = SPELL_RESIST_MAGIC;
    af.duration = level;
    af.modifier = modifier;
    af.location = APPLY_SAVING_SPELL;
    af.bitvector = 0;

    affected_type* resist_magic_effect = affected_by_spell(victim, SPELL_RESIST_MAGIC);
    if (resist_magic_effect) {
        // Refresh resist magic.
        if (resist_magic_effect->modifier <= modifier && resist_magic_effect->duration <= level && resist_magic_effect->duration > 0) {
            // Remove the old affect from the victim.
            affect_modify(victim, resist_magic_effect->location, resist_magic_effect->modifier, resist_magic_effect->bitvector, AFFECT_MODIFY_REMOVE, 0);
            affect_total(victim);

            resist_magic_effect->modifier = modifier;
            resist_magic_effect->duration = level;

            // And add the new affect to the victim.
            affect_modify(victim, resist_magic_effect->location, resist_magic_effect->modifier, resist_magic_effect->bitvector, AFFECT_MODIFY_SET, 0);
            affect_total(victim);

            send_to_char("You refresh your resistance to magic.\n\r", victim);
        } else {
            send_to_char("You are already benefiting from greater resistance.\n\r", victim);
        }
    } else {
        affect_to_char(victim, &af);
        send_to_char("You feel yourself resistant to magic.\n\r", victim);
    }
}

ASPELL(spell_slow_digestion)
{
    struct affected_type af;
    int loc_level;

    if (!victim)
        return;

    int level = get_mystic_caster_level(caster);
    if (victim != caster)
        loc_level = GET_PROF_LEVEL(PROF_CLERIC, victim) + level;
    else
        loc_level = level;

    if (!affected_by_spell(victim, SPELL_SLOW_DIGESTION)) {
        af.type = SPELL_SLOW_DIGESTION;
        af.duration = loc_level + 12;
        af.modifier = loc_level;
        af.location = APPLY_NONE;
        af.bitvector = 0;

        affect_to_char(victim, &af);
        send_to_char("You feel your stomach shrinking.\n\r", victim);
    }
}

ASPELL(spell_divination)
{
    // Ensure that we have a valid caster and location.
    if (caster == NULL || caster->in_room == NOWHERE)
        return;

    char buff[1000];
    std::size_t length = 0;
    auto append_to_buff = [&](const char* text) {
        if (!text || length >= sizeof(buff) - 1) {
            return;
        }

        int written = std::snprintf(buff + length, sizeof(buff) - length, "%s", text);
        if (written < 0) {
            return;
        }

        std::size_t appended = static_cast<std::size_t>(written);
        if (appended >= sizeof(buff) - length) {
            length = sizeof(buff) - 1;
            buff[length] = '\0';
            return;
        }

        length += appended;
    };

    const room_data& cur_room = world[caster->in_room];

    std::snprintf(buff, sizeof(buff), "You feel confident about your location.\n\r");
    sprintbit(cur_room.room_flags, room_bits, buf, 0);
    length = strlen(buff);
    std::snprintf(buff + length, sizeof(buff) - length, " (#%d) [ %s, %s], Exits are:\n\r",
        cur_room.number, sector_types[cur_room.sector_type], buf);
    send_to_char(buff, caster);

    bool found = false;
    for (int dir = 0; dir < NUM_OF_DIRS; dir++) {
        room_direction_data* exit = cur_room.dir_option[dir];
        if (exit && exit->to_room != NOWHERE) {
            const room_data& exit_room = world[exit->to_room];

            found = true;
            std::snprintf(buff, sizeof(buff), "%5s: to %s (#%d)\n\r", dirs[dir], exit_room.name, exit_room.number);
            if (exit->exit_info != 0) {
                const char* keyword = exit->keyword ? exit->keyword : "";
                const char* key_name = "None";
                if (exit->key > 0) {
                    int obj_index = real_object(exit->key);
                    if (obj_index >= 0) {
                        const obj_data& key = obj_proto[obj_index];
                        key_name = key.short_description;
                    } else {
                        std::ostringstream error_log;
                        error_log << "Found a room with an invalid key.  Room Number: " << exit_room.number << " and Room Name: " << exit_room.name << std::endl;
                        std::string msg = error_log.str();
                        log(const_cast<char*>(msg.c_str()));
                        send_to_char("You found a key that shouldn't exist!  Please notify the imm group immediately at rots.management3791@gmail.com with your room name.", caster);
                    }
                }

                length = strlen(buff);
                std::snprintf(buff + length, sizeof(buff) - length, "     door '%s', key '%s'.\n\r",
                    keyword, key_name);
            }
            send_to_char(buff, caster);
        }
    }
    if (!found) {
        send_to_char("None.\n\r", caster);
    }

    std::snprintf(buff, sizeof(buff), "Living beings in the room:\n\r");
    length = strlen(buff);
    if (cur_room.people) {
        for (char_data* character = cur_room.people; character; character = character->next_in_room) {
            if (caster->player.level >= GET_INVIS_LEV(character)) {
                append_to_buff(GET_NAME(character));
                if (character->next_in_room) {
                    append_to_buff(", ");
                } else {
                    append_to_buff(".\n\r");
                }
            }
        }
        send_to_char(buff, caster);
    } else {
        send_to_char("Living beings in the room:\n\r None.\n\r", caster);
    }

    if (cur_room.contents) {
        std::snprintf(buff, sizeof(buff), "Objects in the room:\n\r");
        length = strlen(buff);
        for (obj_data* item = cur_room.contents; item; item = item->next_content) {
            if (CAN_SEE_OBJ(caster, item)) {
                append_to_buff(item->short_description);
                if (item->next_content) {
                    append_to_buff(", ");
                } else {
                    append_to_buff(".\n\r");
                }
            }
        }
        send_to_char(buff, caster);
    } else {
        send_to_char("Objects in the room:\n\r None.\n\r", caster);
    }
}

ASPELL(spell_infravision)
{
    if (!victim)
        victim = caster;

    if (affected_by_spell(victim, SPELL_INFRAVISION)) {
        if (victim == caster)
            send_to_char("You already can see in the dark.\n\r", caster);
        else
            act("$E already can see in the dark.\n\r", TRUE, caster, 0, victim, TO_CHAR);
        return;
    }

    int level = get_mystic_caster_level(caster);
    affected_type af;
    af.type = SPELL_INFRAVISION;
    af.duration = level;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = AFF_INFRARED;

    affect_to_char(victim, &af);
    send_to_char("Your eyes burn.\n\r", victim);
}

/*
 * Regeneration Spells listed below in order
 * - Resist poison
 * - Curing Saturation
 * - Restlessness
 * - Remove Poison
 * - Vitality
 * - Dispel Regeneration
 * - Regeneration
 */

ASPELL(spell_resist_poison)
{
    affected_type* af;
    affected_type newaf;

    if (!victim)
        victim = caster;
    af = affected_by_spell(victim, SPELL_POISON);
    if (af) {
        if (affected_by_spell(victim, SPELL_RESIST_POISON)) {
            send_to_char("The poison is already being resisted.\n\r", caster);
        } else {
            newaf.type = SPELL_RESIST_POISON;
            newaf.duration = af->duration;
            newaf.modifier = GET_PROF_LEVEL(PROF_CLERIC, caster);
            newaf.location = APPLY_NONE;
            newaf.bitvector = 0;
            affect_to_char(victim, &newaf);
            send_to_char("You begin to resist the poison.\n\r", victim);
            if (victim != caster)
                send_to_char("They begin to resist the poison.\n\r", caster);
        }
    } else if (victim == caster)
        send_to_char("But you have not been poisoned!\n\r", caster);
    else
        send_to_char("But they are not poisoned!\n\r", caster);
}

ASPELL(spell_curing)
{
    if (!victim)
        return;

    if (GET_RACE(victim) == RACE_OLOGHAI) {
        send_to_char("You cannot improve an Olog-Hai's regeneration.\n\r", caster);
        return;
    }

    int healing_level = get_mystic_caster_level(caster) + 5;
    if (victim != caster) {
        healing_level = (healing_level + get_mystic_caster_level(victim)) / 2;
    }

    if (utils::get_specialization(*caster) == game_types::PS_Regeneration) {
        healing_level += 6;
    }

    if (!affected_by_spell(victim, SPELL_CURING)) {
        affected_type effect;
        effect.type = SPELL_CURING;
        effect.duration = healing_level * FAST_UPDATE_RATE / 2;
        effect.modifier = healing_level;
        effect.location = APPLY_NONE;
        effect.bitvector = 0;

        affect_to_char(victim, &effect);
        send_to_char("You feel yourself becoming healthier.\n\r", victim);
    } else {
        act("You could not improve $N's healing.", FALSE, caster, 0, victim, TO_CHAR);
    }
}

ASPELL(spell_restlessness)
{
    if (!victim)
        return;

    if (GET_RACE(victim) == RACE_OLOGHAI) {
        send_to_char("You cannot improve an Olog-Hai's vitality.\n\r", caster);
        return;
    }

    int healing_level = get_mystic_caster_level(caster) + 5;
    if (victim != caster) {
        healing_level = (healing_level + get_mystic_caster_level(victim)) / 2;
    }

    if (utils::get_specialization(*caster) == game_types::PS_Regeneration) {
        healing_level += 6;
    }

    if (!affected_by_spell(victim, SPELL_RESTLESSNESS)) {
        affected_type effect;
        effect.type = SPELL_RESTLESSNESS;
        effect.duration = healing_level * FAST_UPDATE_RATE / 2;
        effect.modifier = healing_level;
        effect.location = APPLY_NONE;
        effect.bitvector = 0;

        affect_to_char(victim, &effect);
        send_to_char("You feel yourself lighter.\n\r", victim);
    } else {
        act("You could not ease $N's movement.", FALSE, caster, 0, victim, TO_CHAR);
    }
}

ASPELL(spell_remove_poison)
{

    if (!caster || (!victim && !obj)) {
        mudlog("remove_poison without all arguments.", NRM, 0, 0);
        return;
    }

    if (victim) {
        if (affected_by_spell(victim, SPELL_POISON)) {
            affect_from_char(victim, SPELL_POISON);
            act("A warm feeling runs through your body.", FALSE, victim, 0, 0, TO_CHAR);
            act("$N looks better.", FALSE, caster, 0, victim, TO_ROOM);
        }
    } else {
        if ((obj->obj_flags.type_flag == ITEM_DRINKCON) || (obj->obj_flags.type_flag == ITEM_FOUNTAIN) || (obj->obj_flags.type_flag == ITEM_FOOD)) {
            obj->obj_flags.value[3] = 0;
            act("The $p steams briefly.", FALSE, caster, obj, 0, TO_CHAR);
        }
    }
}

ASPELL(spell_vitality)
{
    if (!victim)
        return;

    if (GET_RACE(victim) == RACE_OLOGHAI) {
        send_to_char("You cannot improve an Olog-Hai's vitality.\n\r", caster);
        return;
    }

    int healing_level = get_mystic_caster_level(caster);
    if (utils::get_specialization(*caster) == game_types::PS_Regeneration) {
        healing_level += 6;
    }

    healing_level = healing_level / 3 * (SECS_PER_MUD_HOUR * 4) / PULSE_FAST_UPDATE;
    affected_type* current_vitality_effect = affected_by_spell(victim, SPELL_VITALITY);
    if (!current_vitality_effect) {
        affected_type vitality_effect;
        vitality_effect.type = SPELL_VITALITY;
        vitality_effect.duration = healing_level;
        vitality_effect.modifier = 1;
        vitality_effect.location = APPLY_NONE;
        vitality_effect.bitvector = 0;

        affect_to_char(victim, &vitality_effect);
        send_to_char("You feel yourself becoming much lighter.\n\r", victim);
    } else if (current_vitality_effect->duration < healing_level / 2) {
        current_vitality_effect->duration = healing_level;
        send_to_char("You feel another surge of lightness.\n\r", victim);
        act("You renew $N's vitality.", FALSE, caster, 0, victim, TO_CHAR);
    } else {
        if (victim == caster) {
            send_to_char("You are still as light as can be.\n\r", caster);
        } else {
            act("You could not improve $N's vitality.", FALSE, caster, 0, victim, TO_CHAR);
        }
    }
}

ASPELL(spell_dispel_regeneration)
{
    if (!victim) {
        send_to_char("Whom do you want to dispel?\n\r", caster);
        return;
    }

    if (victim == caster) {
        affect_from_char(victim, SPELL_RESTLESSNESS);
        affect_from_char(victim, SPELL_CURING);
        affect_from_char(victim, SPELL_REGENERATION);
        affect_from_char(victim, SPELL_VITALITY);
        return;
    } else {
        affected_type* spell = utils::is_affected_by_spell(*victim, SPELL_RESTLESSNESS);
        if (spell) {
            if (saves_mystic(victim) && other_side(caster, victim)) {
                act("$N resists your attempt to dispel Restlessness.", FALSE, caster, 0, victim, TO_CHAR);
                act("You resist $n's attempts to dispel Restlessness from you.", FALSE, caster, 0, victim, TO_VICT);
            } else {
                affect_remove(victim, spell);
            }
        }

        spell = utils::is_affected_by_spell(*victim, SPELL_CURING);
        if (spell) {
            if (saves_mystic(victim) && other_side(caster, victim)) {
                act("$N resists your attempt to dispel Curing Saturation.", FALSE, caster, 0, victim, TO_CHAR);
                act("You resist $n's attempts to dispel Curing Saturation from you.", FALSE, caster, 0, victim, TO_VICT);
            } else {
                affect_remove(victim, spell);
            }
        }

        spell = utils::is_affected_by_spell(*victim, SPELL_REGENERATION);
        if (spell) {
            if (saves_mystic(victim) && other_side(caster, victim)) {
                act("$N resists your attempt to dispel Regeneration.", FALSE, caster, 0, victim, TO_CHAR);
                act("You resist $n's attempts to dispel Regeneration from you.", FALSE, caster, 0, victim, TO_VICT);
            } else {
                affect_remove(victim, spell);
            }
        }

        spell = utils::is_affected_by_spell(*victim, SPELL_VITALITY);
        if (spell) {
            if (saves_mystic(victim) && other_side(caster, victim)) {
                act("$N resists your attempt to dispel Vitality.", FALSE, caster, 0, victim, TO_CHAR);
                act("You resist $n's attempts to dispel Vitality from you.", FALSE, caster, 0, victim, TO_VICT);
            } else {
                affect_remove(victim, spell);
            }
        }
    }
}

ASPELL(spell_regeneration)
{
    if (!victim)
        return;

    if (GET_RACE(victim) == RACE_OLOGHAI) {
        send_to_char("You cannot improve an Olog-Hai's regeneration.\n\r", caster);
        return;
    }

    int regen_level = get_mystic_caster_level(caster) - 10;
    if (utils::get_specialization(*caster) == game_types::PS_Regeneration) {
        regen_level += 6;
    }

    regen_level = regen_level / 2 * FAST_UPDATE_RATE;

    affected_type* current_regen_effect = affected_by_spell(victim, SPELL_REGENERATION);
    if (!current_regen_effect) {
        affected_type regen_effect;
        regen_effect.type = SPELL_REGENERATION;
        regen_effect.duration = regen_level;
        regen_effect.modifier = 1;
        regen_effect.location = APPLY_NONE;
        regen_effect.bitvector = 0;

        affect_to_char(victim, &regen_effect);
        send_to_char("You feel yourself becoming much healthier.\n\r", victim);
    } else if (current_regen_effect->duration < regen_level / 2) {
        current_regen_effect->duration = regen_level;
        send_to_char("You feel another surge of energy.\n\r", victim);
        act("You renew $N's regeneration.", FALSE, caster, 0, victim, TO_CHAR);
    } else {
        if (victim == caster) {
            send_to_char("You are still regenerating fast enough.\n\r", caster);
        } else {
            act("You could not improve $N's regeneration.", FALSE, caster, 0, victim, TO_CHAR);
        }
    }
}

void cast_mass_spell(char_data* caster, void (*spell)(char_data* caster, char* arg, int type, char_data* victim, obj_data* obj, int digit, int is_object))
{
    if (!caster->group) {
        send_to_char("You are not in a group.\n\r", caster);
        return;
    }

    for (auto iter = caster->group->begin(); iter != caster->group->end(); ++iter) {
        char_data* group_member = *iter;
        if (group_member->in_room == caster->in_room) {
            spell(caster, nullptr, SPELL_TYPE_SPELL, group_member, nullptr, 0, 0);
        }
    }
}

ASPELL(spell_mass_regeneration)
{
    if (utils::get_skill(*caster, SPELL_REGENERATION) < 100) {
        send_to_char("Only those who have mastered regeneration may cast this spell.\n\r", caster);
        return;
    }
    send_to_room("The air hums as a powerful magic breathes life into all who stand within its embrace.\n\r", caster->in_room);
    cast_mass_spell(caster, spell_regeneration);
}

ASPELL(spell_mass_vitality)
{
    if (utils::get_skill(*caster, SPELL_VITALITY) < 100) {
        send_to_char("Only those who have mastered vitality may cast this spell.\n\r", caster);
        return;
    }
    send_to_room("The air hums as a powerful magic breathes vitality into all who stand within its embrace.\n\r", caster->in_room);
    cast_mass_spell(caster, spell_vitality);
}

ASPELL(spell_mass_insight)
{
    if (utils::get_skill(*caster, SPELL_INSIGHT) < 100) {
        send_to_char("Only those who have mastered insight may cast this spell.\n\r", caster);
        return;
    }
    send_to_room("The air hums as a powerful magic brings insight into all who stand within its embrace.\n\r", caster->in_room);
    cast_mass_spell(caster, spell_insight);
}

/*
 *  Offensive Spells listed below in order
 *  - Hallucinate
 *  - Haze
 *  - Fear
 *  - Poison
 *  - Terror
 */

ASPELL(spell_hallucinate)
{
    struct affected_type af;
    int loc_level, my_duration;
    int modifier;

    if (!victim)
        return;

    int level = get_mystic_caster_level(caster);
    loc_level = level;
    if (affected_by_spell(victim, SPELL_HALLUCINATE))
        send_to_char("They are already hallucinating!\n\r", caster);

    /*
     *  The modifier represents the number of times that the affected character
     * can "miss" the illusions of the characters/mobiles they're trying to hit.
     * Once they've "missed" this number of times, the effect will be removed.
     * If they hit the player before the modifier is 0, the effect will also be
     * removed.  A player of mystic level 31 or higher gets an additional +1
     * modifier.  A player specialization of Illusion give the player an
     * additional +1 modifier.
     */

    modifier = ((GET_PROF_LEVEL(PROF_CLERIC, caster) / 10) + 2)
        + ((GET_PROF_LEVEL(PROF_CLERIC, caster) > 30) ? 1 : 0)
        + ((GET_SPEC(caster) == PLRSPEC_ILLU) ? 1 : 0);
    my_duration = modifier * 4;

    if (!affected_by_spell(victim, SPELL_HALLUCINATE) && (is_object || !saves_confuse(victim, caster))) {
        af.type = SPELL_HALLUCINATE;
        af.duration = my_duration;
        af.modifier = modifier;
        af.location = APPLY_NONE;
        af.bitvector = AFF_HALLUCINATE;

        affect_to_char(victim, &af);
        act("The world around you blurs and fades.\n\r", TRUE, victim, 0, caster, TO_CHAR);
        act("$n nervously glances around in confusion!", FALSE, victim, 0, 0, TO_ROOM);

        damage(caster, victim, 0, SPELL_HALLUCINATE, 0);
    }
}

ASPELL(spell_haze)
{
    struct affected_type af;
    struct affected_type* oldaf;
    int loc_level, my_duration, tmp;
    affected_type* tmpaf;

    if (!victim && !obj && !(caster->specials.fighting)) { /*hazing the room*/

        if (!caster) {
            return;
        }

        int level = get_mystic_caster_level(caster);
        af.type = ROOMAFF_SPELL;
        af.duration = (level) / 3;
        af.modifier = level / 2;
        af.location = SPELL_HAZE;
        af.bitvector = 0;

        if ((oldaf = room_affected_by_spell(&world[caster->in_room], SPELL_HAZE))) {
            if (oldaf->duration < af.duration) {
                oldaf->duration = af.duration;
            }

            if (oldaf->modifier < af.modifier) {
                oldaf->modifier = af.modifier;
            }
        } else {
            affect_to_room(&world[caster->in_room], &af);
        }

        act("$n breathes out a disorientating mist.", TRUE, caster, 0, 0, TO_ROOM);
        send_to_char("You breathe out a disorientating mist.\n\r", caster);

        return;
    }

    if (!victim && caster->specials.fighting) {
        victim = caster->specials.fighting;
    }

    if ((type == SPELL_TYPE_ANTI) && is_object) {
        for (tmpaf = caster->affected, tmp = 0; (tmp < MAX_AFFECT) && tmpaf;
             tmpaf = tmpaf->next, tmp++)
            if ((tmpaf->type == SPELL_HAZE) && (tmpaf->duration == -1))
                break;

        if (tmpaf)
            affect_remove(caster, tmpaf);

        return;
    } else if (type == SPELL_TYPE_ANTI) {
        affect_from_char(victim, SPELL_HAZE);
        return;
    }

    int level = get_mystic_caster_level(caster);
    if (utils::get_specialization(*caster) == game_types::PS_Illusion) {
        level += 6;
    }
    loc_level = level;

    if (is_object)
        my_duration = -1;
    else
        my_duration = number(0, 1);

    if (!affected_by_spell(victim, SPELL_HAZE) && (is_object || !saves_mystic(victim))) {
        af.type = SPELL_HAZE;
        af.duration = my_duration;
        af.modifier = loc_level;
        af.location = APPLY_NONE;
        af.bitvector = AFF_HAZE;

        affect_to_char(victim, &af);
        act("You feel dizzy as your surroundings seem to blur and twist.\n\r", TRUE, victim, 0, caster, TO_CHAR);
        act("$n staggers, overcome by dizziness!", FALSE, victim, 0, 0, TO_ROOM);
    }
}

ASPELL(spell_fear)
{
    struct affected_type af;

    if (!victim) {
        send_to_char("Whom do you want to scare?\n\r", caster);
        return;
    }

    if (victim == caster) {
        send_to_char("You look upon yourself.\n\rYou are scared to death.\n\r", caster);
        return;
    }

    if (!IS_NPC(caster) && !IS_NPC(victim) && RACE_GOOD(caster) && RACE_GOOD(victim) && GET_LEVEL(caster) < LEVEL_IMMORT && GET_LEVEL(victim) < LEVEL_IMMORT && caster != victim) {
        act("$N is not scared by your spell.",
            FALSE, caster, 0, victim, TO_CHAR);
        act("You are not scared by $n's spell.",
            FALSE, caster, 0, victim, TO_VICT);
        act("$n attempts to scare $N but fails.",
            FALSE, caster, 0, victim, TO_NOTVICT);
        return;
    }

    int level = get_mystic_caster_level(caster);
    if (utils::get_specialization(*caster) == game_types::PS_Illusion) {
        level += 6;
    }
    if (!affected_by_spell(victim, SPELL_FEAR) && !saves_mystic(victim) && !saves_leadership(victim)) {
        af.type = SPELL_FEAR;
        af.duration = level;
        af.modifier = level + 10;
        af.location = APPLY_NONE;
        af.bitvector = 0;

        affect_to_char(victim, &af);
        if ((GET_RACE(caster) == RACE_URUK) || (GET_RACE(caster) == RACE_ORC)) {
            act("$n breathes a vile, putrid breath onto you. "
                "Fear races through your heart!",
                FALSE, caster, 0, victim, TO_VICT);
            act("$n breathes a vile, putrid breath at $N. $N is scared!",
                TRUE, caster, 0, victim, TO_NOTVICT);
        } else {
            act("$n breathes an icy, cold breath onto you. "
                "Fear races through your heart!",
                FALSE, caster, 0, victim, TO_VICT);
            act("$n breathes an icy, cold breath at $N; $N is scared!",
                TRUE, caster, 0, victim, TO_NOTVICT);
        }
        act("$N looks shocked for a brief moment before the panic sets in.",
            FALSE, caster, 0, victim, TO_CHAR);
    } else
        act("$N ignores your breath.", FALSE, caster, 0, victim, TO_CHAR);

    return;
}

ASPELL(spell_poison)
{
    struct affected_type af;
    struct affected_type* oldaf;
    int magus_save = 0;

    if (!victim && !obj && !(caster->specials.fighting)) { /*poisoning the room*/

        if (!caster)
            return;

        int level = get_mystic_caster_level(caster);
        af.type = ROOMAFF_SPELL;
        af.duration = (level) / 3;
        af.modifier = level / 2;
        af.location = SPELL_POISON;
        af.bitvector = 0;

        if ((oldaf = room_affected_by_spell(&world[caster->in_room], SPELL_POISON))) {
            if (oldaf->duration < af.duration)
                oldaf->duration = af.duration;

            if (oldaf->modifier < af.modifier)
                oldaf->modifier = af.modifier;
        } else {
            affect_to_room(&world[caster->in_room], &af);
        }

        act("$n breathes out a cloud of smoke.", TRUE, caster, 0, 0, TO_ROOM);
        send_to_char("You breathe out poison.\n\r", caster);

        return;
    }

    if (!victim && caster->specials.fighting) {
        victim = caster->specials.fighting;
    }

    if (victim) {
        if (!saves_poison(victim, caster) && (number(0, magus_save) < 50)) {
            int level = get_mystic_caster_level(caster);
            af.type = SPELL_POISON;
            af.duration = level + 1;
            af.modifier = -2;
            af.location = APPLY_STR;
            af.bitvector = AFF_POISON;

            affect_join(victim, &af, FALSE, FALSE);

            send_to_char("You feel very sick.\n\r", victim);
            damage((caster) ? caster : victim, victim, 5, SPELL_POISON, 0);
        } else {
            act("You feel your body fend off the poison.", TRUE, caster, 0, victim, TO_VICT);
            act("$N shrugs off your poison with ease.", FALSE, caster, 0, victim, TO_CHAR);
        }

    } else { /* Object poison */
        if ((obj->obj_flags.type_flag == ITEM_DRINKCON) || (obj->obj_flags.type_flag == ITEM_FOUNTAIN) || (obj->obj_flags.type_flag == ITEM_FOOD)) {
            obj->obj_flags.value[3] = 1;
        }
    }
}

ASPELL(spell_terror)
{
    struct char_data* tmpch;
    struct affected_type af;

    if (!caster || (caster->in_room == NOWHERE))
        return;

    send_to_char("You breathe an icy, cold breath across the room.\n\r",
        caster);

    int level = get_mystic_caster_level(caster);
    if (utils::get_specialization(*caster) == game_types::PS_Illusion) {
        level += 6;
    }
    for (tmpch = world[caster->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
        if ((tmpch != caster) && !affected_by_spell(tmpch, SPELL_FEAR)) {
            if (!saves_mystic(tmpch) && !saves_leadership(tmpch)) {
                af.type = SPELL_FEAR;
                af.duration = level;
                af.modifier = level + 10;
                af.location = APPLY_NONE;
                af.bitvector = 0;

                affect_to_char(tmpch, &af);
                act("$n suddenly breathes an icy, cold breath everywhere. "
                    "Terror overcomes you.",
                    FALSE, caster, 0, tmpch, TO_VICT);
            } else
                act("$n suddenly breathes an icy, cold breath. You ignore it.",
                    FALSE, caster, 0, tmpch, TO_VICT);
        }
    }
}

/*
 * Misc Mystic Spells listed below
 *  - Attune
 *  - Sanctuary
 *  - Enchant Weapon
 *  - Death Ward
 *  - Confuse
 *  - Guardian
 *  - Shift -should we remove this spell completely?
 *  - Protection
 */

ASPELL(spell_attune)
{
    struct obj_data* object;

    object = caster->equipment[WIELD];
    if (!(object)) {
        send_to_char("But you are not wielding a weapon!\n\r", caster);
        return;
    }

    SET_BIT(object->obj_flags.extra_flags, ITEM_WILLPOWER);
    object->obj_flags.prog_number = 1;
    send_to_char("You attune your mind to your weapon.\n\r", caster);
}

ASPELL(spell_sanctuary)
{
    struct affected_type af;
    int loc_level;

    if (!victim)
        return;

    if (affected_by_spell(caster, SPELL_ANGER)) {
        send_to_char("Your mind is blinded by anger. "
                     "Try again when you have cooled down.\n\r",
            caster);
        return;
    }
    if (affected_by_spell(victim, SPELL_ANGER)) {
        send_to_char("Your victim's negative energy resists your"
                     " attempts to form your spell.\r\n",
            caster);
        return;
    }

    if (caster == victim)
        loc_level = GET_PROF_LEVEL(PROF_CLERIC, caster);
    else
        loc_level = (std::max(6, GET_PROF_LEVEL(PROF_CLERIC, victim)));

    if (!affected_by_spell(victim, SPELL_SANCTUARY)) {
        af.type = SPELL_SANCTUARY;
        af.duration = loc_level;
        af.modifier = GET_ALIGNMENT(caster);
        af.location = APPLY_NONE;
        af.bitvector = AFF_SANCTUARY;

        affect_to_char(victim, &af);
        send_to_char("You are surrounded by a bright aura.\n\r", victim);
        act("$n is surrounded by a bright aura.", FALSE, victim, 0, 0, TO_ROOM);
    }
}

ASPELL(spell_enchant_weapon)
{
    int i, bonus = 0;

    if (!caster || !obj)
        return;

    assert(MAX_OBJ_AFFECT >= 2);

    if ((GET_ITEM_TYPE(obj) == ITEM_WEAPON) && !IS_SET(obj->obj_flags.extra_flags, ITEM_MAGIC)) {

        for (i = 0; i < MAX_OBJ_AFFECT; i++) {
            if (obj->affected[i].location != APPLY_NONE) {
                utils::add_spirits(caster, 50);
                send_to_char("There is too much magic in it already.\n\r", caster);
                return;
            }
        }

        SET_BIT(obj->obj_flags.extra_flags, ITEM_MAGIC);
        bonus = 6;
        obj->affected[0].location = APPLY_OB;
        obj->affected[0].modifier = bonus;

        if (IS_GOOD(caster)) {
            SET_BIT(obj->obj_flags.extra_flags, ITEM_ANTI_EVIL);
            act("$p glows blue.", FALSE, caster, obj, 0, TO_CHAR);
        } else if (IS_EVIL(caster)) {
            SET_BIT(obj->obj_flags.extra_flags, ITEM_ANTI_GOOD);
            act("$p glows red.", FALSE, caster, obj, 0, TO_CHAR);
        } else
            act("$p glows yellow.", FALSE, caster, obj, 0, TO_CHAR);
    }
}

ASPELL(spell_death_ward)
{
    affected_type af;

    if ((type == SPELL_TYPE_ANTI) || is_object) {

        /* XXX: What the fuck? */
        affect_from_char(victim, SPELL_INSIGHT);

        if (type == SPELL_TYPE_ANTI)
            return;
    }

    int level = get_mystic_caster_level(caster);
    if (!affected_by_spell(victim, SPELL_DEATH_WARD)) {
        af.type = SPELL_DEATH_WARD;
        af.duration = (is_object) ? -1 : level * 2;
        af.modifier = level / 2;
        af.location = 0;
        af.bitvector = 0;

        affect_to_char(victim, &af);
        send_to_char("You feel a ward being woven around you.\n\r", caster);
    } else {
        send_to_char("You are warded already!\n\r", caster);
    }
    return;
}

ASPELL(spell_confuse)
{
    struct affected_type af;
    int loc_level, my_duration, tmp;
    affected_type* tmpaf;
    int modifier;

    if (!victim)
        return;

    if ((type == SPELL_TYPE_ANTI) && is_object) {
        for (tmpaf = caster->affected, tmp = 0; (tmp < MAX_AFFECT) && tmpaf;
             tmpaf = tmpaf->next, tmp++)
            if ((tmpaf->type == SPELL_CONFUSE) && (tmpaf->duration == -1))
                break;

        if (tmpaf)
            affect_remove(caster, tmpaf);

        return;
    } else if (type == SPELL_TYPE_ANTI) {
        affect_from_char(victim, SPELL_CONFUSE);
        return;
    }

    int level = get_mystic_caster_level(caster);
    loc_level = level;

    if (is_object)
        my_duration = -1;
    my_duration = 10 + level;

    modifier = 1;

    if (!affected_by_spell(victim, SPELL_CONFUSE) && (is_object || !saves_confuse(victim, caster))) {
        af.type = SPELL_CONFUSE;
        af.duration = my_duration;
        af.modifier = modifier;
        af.location = APPLY_NONE;
        af.bitvector = AFF_CONFUSE;

        affect_to_char(victim, &af);
        act("Strange thoughts stream through your mind, making it hard to concentrate.\n\r", TRUE,
            victim, 0, caster, TO_CHAR);
        act("$n appears to be confused!", FALSE, victim, 0, 0, TO_ROOM);
    }
}

void set_guardian_stats(const int caster_mystic_level, char_ability_data& guardian_mob_abilities)
{
    const int ability_base = 8 + caster_mystic_level / 4;
    guardian_mob_abilities.str = ability_base;
    guardian_mob_abilities.intel = ability_base;
    guardian_mob_abilities.wil = ability_base;
    guardian_mob_abilities.dex = ability_base;
    guardian_mob_abilities.con = ability_base;
    guardian_mob_abilities.lea = ability_base;
}

int calc_guardian_hp(const int base_hp, const int caster_mystic_level)
{
    double random_factor = number(6.0) - 3.0; // TODO(drelidan): fix number_d with negative "to" number.
    int health = int(base_hp * (caster_mystic_level + random_factor) / 3.0);
    return health;
}

void set_guardian_health(char_data* guardian_mob, int new_health, bool restore_health)
{
    if (restore_health || guardian_mob->abilities.hit < new_health) {
        guardian_mob->abilities.hit = new_health;
        guardian_mob->constabilities.hit = new_health;
    }

    if (restore_health) {
        guardian_mob->tmpabilities.hit = new_health;
    } else {
        guardian_mob->tmpabilities.hit = std::min(guardian_mob->tmpabilities.hit, guardian_mob->abilities.hit);
    }
}

void tweak_aggressive_guardian_stats(const int caster_mystic_level, char_data* guardian_mob, bool restore_health)
{
    guardian_mob->abilities.str += 5;
    guardian_mob->abilities.wil -= 5;

    guardian_mob->tmpabilities.str += 5;
    guardian_mob->tmpabilities.wil -= 5;

    guardian_mob->constabilities.str += 5;
    guardian_mob->constabilities.wil -= 5;

    const int BASE_OB = 13;
    guardian_mob->points.OB = BASE_OB * caster_mystic_level / 5;
    guardian_mob->points.parry = 3 + caster_mystic_level / 2;
    guardian_mob->points.dodge = caster_mystic_level / 10;
    guardian_mob->points.damage = (caster_mystic_level / 3) + 1;

    const int BASE_HP = 9;
    int health = calc_guardian_hp(BASE_HP, caster_mystic_level);
    set_guardian_health(guardian_mob, health, restore_health);
}

void tweak_defensive_guardian_stats(const int caster_mystic_level, char_data* guardian_mob, bool restore_health)
{
    guardian_mob->abilities.wil -= 5;
    guardian_mob->tmpabilities.wil -= 5;
    guardian_mob->constabilities.wil -= 5;

    guardian_mob->points.OB = (caster_mystic_level / 2) - 2;
    guardian_mob->points.parry = 8 + caster_mystic_level * 2;
    guardian_mob->points.dodge = 8 + caster_mystic_level;
    guardian_mob->points.damage = (caster_mystic_level / 6) + 1;

    const int BASE_HP = 22;
    int health = calc_guardian_hp(BASE_HP, caster_mystic_level);
    set_guardian_health(guardian_mob, health, restore_health);
}

void tweak_mystic_guardian_stats(const int caster_mystic_level, char_data* guardian_mob, bool restore_health)
{
    guardian_mob->abilities.wil += 5;
    guardian_mob->tmpabilities.wil += 5;
    guardian_mob->constabilities.wil += 5;

    guardian_mob->points.OB = 0;
    guardian_mob->points.dodge = 0;
    guardian_mob->points.parry = 0;

    guardian_mob->points.damage = (caster_mystic_level / 6);

    const int BASE_HP = 5;
    int health = calc_guardian_hp(BASE_HP, caster_mystic_level);
    set_guardian_health(guardian_mob, health, restore_health);
}

void scale_guardian(int guardian_type, const char_data* caster, char_data* guardian_mob, bool restore_health)
{
    const int caster_mystic_level = utils::get_prof_level(PROF_CLERIC, *caster);
    guardian_mob->player.level = caster_mystic_level / 2;
    set_guardian_stats(caster_mystic_level, guardian_mob->constabilities);
    set_guardian_stats(caster_mystic_level, guardian_mob->abilities);
    set_guardian_stats(caster_mystic_level, guardian_mob->tmpabilities);

    int base_energy_regen = 60;
    switch (guardian_type) {
    case AGGRESSIVE_GUARDIAN:
        tweak_aggressive_guardian_stats(caster_mystic_level, guardian_mob, restore_health);
        base_energy_regen = 70;
        break;
    case DEFENSIVE_GUARDIAN:
        tweak_defensive_guardian_stats(caster_mystic_level, guardian_mob, restore_health);
        break;
    case MYSTIC_GUARDIAN:
        tweak_mystic_guardian_stats(caster_mystic_level, guardian_mob, restore_health);
        break;
    default:
        break;
    }

    guardian_mob->points.willpower = sh_int(guardian_mob->player.level + guardian_mob->abilities.wil);
    guardian_mob->points.ENE_regen = base_energy_regen + caster_mystic_level;
}

ASPELL(spell_guardian)
{
    /*
     * Guardian now takes an extra
     * argument from the character on cast
     * The argument determines the type of
     * Guardian mob cast by the user
     */

    static char* guardian_type[] = {
        "aggressive",
        "defensive",
        "mystic",
        "\n"
    };
    char_data* tmpch;
    char_data* guardian;
    char first_word[255];
    int guardian_to_load;
    int guardian_num = 1110;

    if (GET_SPEC(caster) != PLRSPEC_GRDN) {
        send_to_char("You are not dedicated enough to cast this.\r\n", caster);
        return;
    }

    /*
     * Takes the extra arguement from the user
     * Checks if its a valid arguement
     * Loads appropriate guardian number according
     * to race/type.
     */

    one_argument(arg, first_word);
    guardian_to_load = search_block(first_word, guardian_type, 0);

    if (guardian_to_load == -1) {
        send_to_char("You'll have to be more specific about "
                     "which guardian you wish to summon.\n",
            caster);
        utils::add_spirits(caster, 30);
        return;
    } else
        guardian_num = guardian_mob[GET_RACE(caster)][guardian_to_load];

    if (caster->in_room == NOWHERE)
        return;
    for (tmpch = character_list; tmpch; tmpch = tmpch->next)
        if ((tmpch->master == caster) && utils::is_guardian(*tmpch))
            break;

    if (tmpch) {
        send_to_char("You already have a guardian.\n\r", caster);
        return;
    }

    if (!(guardian = read_mobile(guardian_num, VIRT))) {
        send_to_char("Could not find a guardian for you, please report.\n\r", caster);
        return;
    }

    char_to_room(guardian, caster->in_room);
    act("$n appears with a flash.", FALSE, guardian, 0, 0, TO_ROOM);
    add_follower(guardian, caster, FOLLOW_MOVE);
    SET_BIT(guardian->specials.affected_by, AFF_CHARM);
    SET_BIT(MOB_FLAGS(guardian), MOB_PET);
    scale_guardian(guardian_to_load, caster, guardian, true);
}

ASPELL(spell_shift)
{
    follow_type* tmpfol;

    if (GET_LEVEL(caster) < LEVEL_IMMORT) {
        send_to_char("You try to shift, but only give yourself a headache.\n\r", caster);
        return;
    }

    if (caster->specials.fighting) {
        send_to_char("You need to be still to cast this!\n\r", caster);
        return;
    }

    if ((GET_LEVEL(caster) < LEVEL_IMMORT) && (victim)) {
        send_to_char("You can only cast this on yourself!\n\r", caster);
        return;
    }

    if (IS_SET(PLR_FLAGS(victim), PLR_ISSHADOW)) {
        REMOVE_BIT(PLR_FLAGS(victim), PLR_ISSHADOW);
        GET_COND(victim, FULL) = 1;
        GET_COND(victim, THIRST) = 1;
    } else {
        SET_BIT(PLR_FLAGS(victim), PLR_ISSHADOW);
        if (IS_RIDING(victim))
            stop_riding(victim);
        if (affected_by_spell(victim, SPELL_MIND_BLOCK))
            affect_from_char(victim, SPELL_MIND_BLOCK);
        if (affected_by_spell(victim, SPELL_SANCTUARY))
            affect_from_char(victim, SPELL_SANCTUARY);
        for (tmpfol = victim->followers; tmpfol; tmpfol = victim->followers)
            stop_follower(tmpfol->follower, FOLLOW_MOVE);
        if (victim->group)
            remove_character_from_group(victim, victim->get_group_leader());
        if (IS_AFFECTED(victim, AFF_HUNT))
            REMOVE_BIT(victim->specials.affected_by, AFF_HUNT);
        if (IS_AFFECTED(victim, AFF_SNEAK))
            REMOVE_BIT(victim->specials.affected_by, AFF_SNEAK);
    }
}

ASPELL(spell_protection)
{
    static char* protection_sphere[] = {
        "fire",
        "cold",
        "lightning",
        "physical",
        "\n"
    };

    char_data* loc_victim;
    int res;
    char first_word[255], second_word[255];
    affected_type newaf;

    if (strlen(arg) >= 255)
        arg[254] = 0;

    half_chop(arg, first_word, second_word);
    fprintf(stderr, "protection: %s, %s\n", first_word, second_word);

    res = search_block(first_word, protection_sphere, 0);

    one_argument(second_word, first_word); // first_word now has the victim

    if (!*first_word)
        loc_victim = caster;
    else
        loc_victim = get_char_room_vis(caster, first_word, 0);

    if (!loc_victim) {
        send_to_char("Nobody here by that name.\n\r", caster);
        return;
    }

    if (affected_by_spell(loc_victim, SPELL_PROTECTION, 0)) {
        if (loc_victim == caster)
            send_to_char("You have protection already.\n\r", caster);
        else
            act("$N has $S protection already.", FALSE, caster, 0, loc_victim, TO_CHAR);

        return;
    }

    int level = get_mystic_caster_level(caster);
    switch (res) {
    case -1:
        send_to_char("You can master protection from fire, cold, lightning or physical only.\n\r", caster);
        break;

    case 0: /* fire */

        newaf.type = SPELL_PROTECTION;
        newaf.duration = (is_object) ? -1 : level * 2;
        newaf.modifier = PLRSPEC_FIRE;
        newaf.location = APPLY_RESIST;
        newaf.bitvector = 0;

        affect_to_char(loc_victim, &newaf);
        send_to_char("You feel resistant to fire!\n\r", loc_victim);

        if (caster != loc_victim)
            act("You grant $N resistance to fire.", FALSE, caster, 0, loc_victim, TO_CHAR);

        break;
    case 1: /* cold */

        newaf.type = SPELL_PROTECTION;
        newaf.duration = (is_object) ? -1 : level * 2;
        newaf.modifier = PLRSPEC_COLD;
        newaf.location = APPLY_RESIST;
        newaf.bitvector = 0;

        affect_to_char(loc_victim, &newaf);
        send_to_char("You feel resistant to cold!\n\r", loc_victim);

        if (caster != loc_victim)
            act("You grant $N resistance to cold.", FALSE, caster, 0, loc_victim, TO_CHAR);

        break;

    case 2: /*lightning*/
        newaf.type = SPELL_PROTECTION;
        newaf.duration = (is_object) ? -1 : level * 2;
        newaf.modifier = PLRSPEC_LGHT;
        newaf.location = APPLY_RESIST;
        newaf.bitvector = 0;

        affect_to_char(loc_victim, &newaf);
        send_to_char("You feel resistant to lightning!\n\r", loc_victim);

        if (caster != loc_victim)
            act("You grant $N resistance to lightning.", FALSE, caster, 0, loc_victim, TO_CHAR);

        break;

    case 3: /* physical */
        newaf.type = SPELL_PROTECTION;
        newaf.duration = (is_object) ? -1 : level * 2;
        newaf.modifier = PLRSPEC_WILD;
        newaf.location = APPLY_RESIST;
        newaf.bitvector = 0;

        affect_to_char(loc_victim, &newaf);
        send_to_char("You feel resistant to physical harm!\n\r", loc_victim);

        if (caster != loc_victim)
            act("You grant $N resistance to physical harm.", FALSE, caster, 0, loc_victim, TO_CHAR);

        break;

    default:
        return;
    };
}

void do_renounce(char_data* character, char* argument, waiting_type* wait_list, int command, int sub_command)
{

    if (utils::is_affected_by_spell(*character, SPELL_SANCTUARY)) {
        send_to_char("You renounce your sanctuary!\n\r", character);
        act("$n renounces $s sanctuary!", FALSE, character, nullptr, nullptr, TO_ROOM);
        affect_from_char(character, SPELL_SANCTUARY);
        REMOVE_BIT(character->specials.affected_by, AFF_SANCTUARY);
        return;
    }

    send_to_char("You renounce yourself to the world, but nothing happens...\n\r", character);
}
