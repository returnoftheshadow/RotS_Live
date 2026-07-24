#include <iostream>
#include <string>

#include "base_utils.h"
#include "big_brother.h"
#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "skill_timer.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct room_data world;
extern struct char_data* waiting_list;
extern struct skill_data skills[];
void appear(struct char_data* ch);
int check_overkill(struct char_data* ch);
const int constexpr FRENZY_TIMER = 600;
const int constexpr SMASH_TIMER = 120;
const int constexpr STOMP_TIMER = 120;
const int constexpr CLEAVE_TIMER = 60;
const int constexpr OVERRUN_TIMER = 120;
ACMD(do_dismount);
ACMD(do_move);

namespace olog_hai {
int get_prob_skill(char_data* attacker, char_data* victim, int skill)
{
    int prob = utils::get_skill(*attacker, skill);
    prob -= get_real_dodge(victim) / 2;
    prob -= get_real_parry(victim) / 2;
    prob += get_real_OB(attacker) / 2;
    prob += number(1, 100);
    prob -= 120;
    return prob;
}

void apply_victim_delay(char_data* victim, int delay)
{
    if (IS_NPC(victim) && MOB_FLAGGED(victim, MOB_NOBASH)) {
        return;
    }
    if (IS_SET(victim->specials.affected_by, AFF_BASH)) {
        return;
    }
    WAIT_STATE_FULL(victim, delay, CMD_BASH, 2, 80, 0, 0, 0, AFF_WAITING | AFF_BASH, TARGET_IGNORE);
}

bool is_skill_valid(char_data* ch, const int& skill_id)
{
    if (utils::get_race(*ch) != RACE_OLOGHAI) {
        send_to_char("Unrecognized command.\r\n", ch);
        return false;
    }

    if (utils::is_shadow(*ch)) {
        send_to_char("Hmm, perhaps you've spent to much time in the mortal lands.\r\n", ch);
        return false;
    }

    const room_data& room = world[ch->in_room];
    if (utils::is_set(room.room_flags, (long)PEACEROOM)) {
        send_to_char("A peaceful feeling overwhelms you, and you cannot bring "
                     "yourself to attack.\r\n",
            ch);
        return false;
    }

    if (utils::get_skill(*ch, skill_id) == 0) {
        std::string message;
        switch (skill_id) {
        case SKILL_SMASH:
            message = "Learn how to smash first.\r\n";
            break;
        case SKILL_FRENZY:
            message = "Learn how to frenzy first.\r\n";
            break;
        case SKILL_STOMP:
            message = "Learn how to stomp first.\r\n";
            break;
        case SKILL_CLEAVE:
            message = "Learn how to cleave first.\r\n";
            break;
        case SKILL_OVERRUN:
            message = "Learn how to overrun first.\r\n";
            break;
        }

        send_to_char(message.c_str(), ch);
        return false;
    }

    obj_data* weapon = ch->equipment[WIELD];
    if (!weapon) {
        send_to_char("Wield a weapon first!\r\n", ch);
        return false;
    }
    return true;
}

char_data* is_target_valid(char_data* attacker, waiting_type* target)
{
    char_data* victim = nullptr;

    if (target->targ1.type == TARGET_TEXT) {
        victim = get_char_room_vis(attacker, target->targ1.ptr.text->text);
    } else if (target->targ1.type == TARGET_CHAR) {
        if (char_exists(target->targ1.ch_num)) {
            victim = target->targ1.ptr.ch;
        }
    }

    return victim;
}

bool is_target_in_room(char_data* attacker, char_data* victim)
{
    return attacker->in_room == victim->in_room;
}

char_data* is_smash_target_valid(char_data* attacker, waiting_type* target)
{
    char_data* victim = is_target_valid(attacker, target);

    if (victim == nullptr) {
        if (attacker->specials.fighting) {
            victim = attacker->specials.fighting;
        } else {
            send_to_char("Smash who?\r\n", attacker);
            return nullptr;
        }
    }

    if (!is_target_in_room(attacker, victim)) {
        send_to_char("Your victim is no longer here.\r\n", attacker);
        return nullptr;
    }

    if (!CAN_SEE(attacker, victim)) {
        send_to_char("Smash who?\r\n", attacker);
        return nullptr;
    }

    if (attacker == victim) {
        send_to_char("Funny aren't we?\r\n", attacker);
        return nullptr;
    }

    return victim;
}

void do_sanctuary_check(char_data* ch)
{
    if (utils::is_affected_by(*ch, AFF_SANCTUARY)) {
        appear(ch);
        send_to_char("You cast off your santuary!\r\n", ch);
        act("$n renouces $s sanctuary!", FALSE, ch, 0, 0, TO_ROOM);
    }
}

char_data* get_random_target(char_data* ch, char_data* original_victim)
{
    char_data* t;
    int num = 0;
    for (t = world[ch->in_room].people; t != nullptr; t = t->next_in_room) {
        if (t != ch && t != original_victim)
            num++;
    }

    if (!num) {
        return original_victim;
    }

    num = number(1, num);
    for (t = world[ch->in_room].people; t != nullptr; t = t->next_in_room) {
        if (t != ch && t != original_victim) {
            --num;
            if (!num) {
                break;
            }
        }
    }
    return t;
}

int get_base_skill_damage(char_data& olog_hai, int prob)
{
    int base_damage = (2 + utils::get_prof_level(PROF_WARRIOR, olog_hai));
    base_damage *= (100 + prob);
    base_damage /= (1000 / utils::get_tactics(olog_hai));
    if (utils::is_twohanded(olog_hai)) {
        base_damage *= 3 / 2;
    }

    if (utils::is_affected_by_spell(olog_hai, SKILL_FRENZY)) {
        base_damage *= 1.10;
    }

    return base_damage;
}

int calculate_overrun_damage(char_data& attacker, int prob)
{
    int damage = get_base_skill_damage(attacker, prob);
    if (utils::get_specialization(attacker) == game_types::PS_HeavyFighting) {
        damage *= 1.10;
    }

    if (utils::is_riding(attacker)) {
        damage *= 1.25;
    }

    return damage;
}

int calculate_smash_damage(char_data& attacker, int prob)
{
    int damage = get_base_skill_damage(attacker, prob);
    if (utils::get_specialization(attacker) == game_types::PS_WildFighting) {
        damage += 5;
    }
    return damage;
}

void generate_smash_dismount_messages(char_data* attacker, char_data* victim)
{
    sprintf(buf, "You smash into $N so hard, it knocks $M to the ground!");
    act(buf, FALSE, attacker, NULL, victim, TO_CHAR);
    sprintf(buf, "$n smashes into you so hard, it knocks prone to the ground!");
    act(buf, FALSE, attacker, NULL, victim, TO_VICT);
    sprintf(buf, "$n smashes into $N so hard, it knocks $M to the ground!");
    act(buf, TRUE, attacker, 0, victim, TO_NOTVICT);
}

void apply_smash_damage(char_data* attacker, char_data* victim, int prob)
{
    int dam = calculate_smash_damage(*attacker, prob);
    if (IS_RIDING(victim) && number() >= 0.80) {
        dam *= 1.25;
        char_data* mount = victim->mount_data.mount;
        damage(attacker, victim, dam, SKILL_SMASH, 0);
        generate_smash_dismount_messages(attacker, victim);
        do_dismount(victim, "", 0, 0, 0);
        if (number() >= 0.50) {
            damage(attacker, mount, dam / 2, SKILL_SMASH, 0);
        }
        return;
    }
    damage(attacker, victim, dam, SKILL_SMASH, 0);
}

int calculate_cleave_damage(char_data& attacker, int prob)
{
    int damage = get_base_skill_damage(attacker, prob);

    if (utils::get_specialization(attacker) == game_types::PS_HeavyFighting) {
        damage += 5;
    }

    return damage;
}

int calculate_stomp_damage(char_data& attacker, int prob)
{
    return get_base_skill_damage(attacker, prob) / 2;
}

void apply_stomp_affect(char_data* attacker, char_data* victim)
{
    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (!bb_instance.is_target_valid(attacker, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            attacker);
        return;
    }

    int prob = get_prob_skill(attacker, victim, SKILL_STOMP);

    if (prob < 0) {
        damage(attacker, victim, 0, SKILL_STOMP, 0);
        return;
    }
    int wait_delay = number(8, PULSE_VIOLENCE);
    apply_victim_delay(victim, wait_delay);
    damage(attacker, victim, calculate_stomp_damage(*attacker, prob), SKILL_STOMP, 0);
}

void apply_overrun_damage(char_data* attacker, char_data* victim)
{
    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (!bb_instance.is_target_valid(attacker, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            attacker);
        return;
    }
    int prob = get_prob_skill(attacker, victim, SKILL_OVERRUN);

    if (prob < 0) {
        damage(attacker, victim, 0, SKILL_OVERRUN, 0);
        return;
    }

    int wait_delay = PULSE_VIOLENCE;
    apply_victim_delay(victim, wait_delay);
    damage(attacker, victim, calculate_overrun_damage(*attacker, prob), SKILL_OVERRUN, 0);
}

void apply_cleave_damage(char_data* attacker, char_data* victim)
{
    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();

    if (!bb_instance.is_target_valid(attacker, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            attacker);
        return;
    }

    int prob = get_prob_skill(attacker, victim, SKILL_CLEAVE);

    if (prob < 0) {
        damage(attacker, victim, 0, SKILL_CLEAVE, 0);
        return;
    }

    damage(attacker, victim, calculate_cleave_damage(*attacker, prob), SKILL_CLEAVE, 0);
}

void generate_frenzy_message(char_data* character)
{
    sprintf(buf, "You enter a frenzied state, filling your body with "
                 "overwhelming power!\r\n");
    act(buf, FALSE, character, NULL, NULL, TO_CHAR);
    sprintf(buf, "$n enters a frenzied state, making $s strikes grow with fervor!\r\n");
    act(buf, TRUE, character, 0, NULL, TO_ROOM);
}

void apply_frenzy_affect(char_data* character)
{
    // generate messages
    generate_frenzy_message(character);
    // create affect
    struct affected_type af;
    af.type = SKILL_FRENZY;
    af.duration = 20;
    af.modifier = 20;
    af.location = APPLY_NONE;
    af.bitvector = 0;
    // apply affect
    affect_to_char(character, &af);
    SET_TACTICS(character, TACTICS_BERSERK);
}

void room_target(char_data* ch, void (*skill_damage)(char_data* character, char_data* victim))
{
    char_data* victim = nullptr;
    char_data* nxt_victim = nullptr;
    auto mount = ch->mount_data.mount;
    for (victim = world[ch->in_room].people; victim; victim = nxt_victim) {
        nxt_victim = victim->next_in_room;
        if (victim != ch && mount != victim) {
            skill_damage(ch, victim);
        }
    }
}
} // namespace olog_hai

ACMD(do_cleave)
{
    one_argument(argument, arg);

    if (!olog_hai::is_skill_valid(ch, SKILL_CLEAVE)) {
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();

    if (!timer.is_skill_allowed(*ch, SKILL_CLEAVE)) {
        send_to_char("You can't use this skill yet.\r\n", ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);
    send_to_char("You arch back and swing your weapon with great velocity!\n\r", ch);
    olog_hai::room_target(ch, &olog_hai::apply_cleave_damage);
    timer.add_skill_timer(*ch, SKILL_CLEAVE, CLEAVE_TIMER);
    // Stun the Olog-hai after using the skill
    int wait_delay = number(8, PULSE_VIOLENCE);
    WAIT_STATE_FULL(ch, wait_delay, 0, 0, 59, 0, 0, 0, AFF_WAITING, TARGET_NONE);
}

ACMD(do_smash)
{
    one_argument(argument, arg);

    if (!olog_hai::is_skill_valid(ch, SKILL_SMASH)) {
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();

    if (!timer.is_skill_allowed(*ch, SKILL_SMASH)) {
        send_to_char("You can't use this skill yet.\r\n", ch);
        return;
    }

    char_data* victim = olog_hai::is_smash_target_valid(ch, wtl);

    if (victim == nullptr) {
        return;
    }

    game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
    if (!bb_instance.is_target_valid(ch, victim)) {
        send_to_char("You feel the Gods looking down upon you, and protecting your "
                     "target.\r\n",
            ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);

    int prob = olog_hai::get_prob_skill(ch, victim, SKILL_SMASH);
    timer.add_skill_timer(*ch, SKILL_SMASH, SMASH_TIMER);

    if (prob < 0) {
        damage(ch, victim, 0, SKILL_SMASH, 0);
        return;
    }

    olog_hai::apply_smash_damage(ch, victim, prob);
}

bool is_direction_valid(char_data* ch, int cmd)
{
    if (!world[ch->in_room].dir_option[cmd]) {
        send_to_char("You cannot go that way.\n\r", ch);
        return false;
    } else if (world[ch->in_room].dir_option[cmd]->to_room == NOWHERE) {
        send_to_char("You cannot go that way.\n\r", ch);
        return false;
    }

    if (!CAN_GO(ch, cmd)) {
        if (IS_SET(EXIT(ch, cmd)->exit_info, EX_ISHIDDEN) && !PRF_FLAGGED(ch, PRF_HOLYLIGHT)) {
            send_to_char("You cannot go that way.\n\r", ch);
            return false;
        } else if (EXIT(ch, cmd)->keyword) {
            if (IS_SHADOW(ch))
                sprintf(buf2, "You cannot pass through the %s.\n\r", fname(EXIT(ch, cmd)->keyword));
            else
                sprintf(buf2, "The %s seems to be closed.\n\r", fname(EXIT(ch, cmd)->keyword));
            send_to_char(buf2, ch);
            return false;
        } else {
            send_to_char("It seems to be closed.\n\r", ch);
            return false;
        }
    } else if (EXIT(ch, cmd)->to_room == NOWHERE) {
        send_to_char("You cannot go that way.\n\r", ch);
        return false;
    }
    return true;
}

int get_direction(std::string direction)
{
    for (auto& c : direction)
        c = toupper(c);

    if (direction == "WEST" || direction == "W") {
        return WEST;
    } else if (direction == "EAST" || direction == "E") {
        return EAST;
    } else if (direction == "NORTH" || direction == "N") {
        return NORTH;
    } else if (direction == "SOUTH" || direction == "S") {
        return SOUTH;
    } else if (direction == "UP" || direction == "U") {
        return UP;
    } else if (direction == "DOWN" || direction == "D") {
        return DOWN;
    }

    return -1;
}

ACMD(do_overrun)
{
    one_argument(argument, arg);
    cmd = get_direction(arg);
    int olog_delay = PULSE_VIOLENCE;

    if (cmd == -1) {
        send_to_char("You don't know what direction that is?!\r\n", ch);
        return;
    }
    if (!olog_hai::is_skill_valid(ch, SKILL_OVERRUN)) {
        return;
    }
    if (!is_direction_valid(ch, cmd)) {
        return;
    }
    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    if (!timer.is_skill_allowed(*ch, SKILL_OVERRUN)) {
        send_to_char("You cannot use this skill yet.\r\n", ch);
        return;
    }
    int total_moves = utils::get_prof_level(PROF_WARRIOR, *ch) / 8 + number(-1, 1);
    int loop_moves = 0;
    int dis;
    char_data* tmpch = nullptr;

    // WARNING: Due to bug in overrun, we need to remove hunt aff
    // otherwise the olog-hai will hit the same target multiple times in a row.
    if (utils::is_affected_by(*ch, AFF_HUNT)) {
        REMOVE_BIT(ch->specials.affected_by, AFF_HUNT);
    }

    for (dis = 0; dis <= total_moves; dis++) {
        olog_hai::room_target(ch, &olog_hai::apply_overrun_damage);

        if (!CAN_GO(ch, cmd)) {
            break;
        }
        for (tmpch = world[ch->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
            if (tmpch->specials.fighting == ch) {
                stop_fighting(tmpch);
            }
        }

        stop_fighting(ch);
        do_move(ch, "", 0, cmd + 1, 0);
    }

    if (dis < total_moves) {
        damage(ch, ch, 50, SKILL_OVERRUN, 0);
        olog_delay *= 1.5;
    } else {
        olog_hai::room_target(ch, &olog_hai::apply_overrun_damage);
    }
    WAIT_STATE_FULL(ch, olog_delay, 0, 0, 1, 0, 0, 0, AFF_WAITING, TARGET_NONE);
    timer.add_skill_timer(*ch, SKILL_OVERRUN, OVERRUN_TIMER);
}

ACMD(do_frenzy)
{
    one_argument(argument, arg);

    if (!olog_hai::is_skill_valid(ch, SKILL_FRENZY)) {
        return;
    }

    if (affected_by_spell(ch, SKILL_FRENZY)) {
        send_to_char("You are already in a frenzy!\r\n", ch);
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    if (!timer.is_skill_allowed(*ch, SKILL_FRENZY)) {
        send_to_char("You cannot use this skill yet.\r\n", ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);
    olog_hai::apply_frenzy_affect(ch);
    timer.add_skill_timer(*ch, SKILL_FRENZY, FRENZY_TIMER);
}

ACMD(do_stomp)
{
    one_argument(argument, arg);
    if (!olog_hai::is_skill_valid(ch, SKILL_STOMP)) {
        return;
    }

    game_timer::skill_timer& timer = game_timer::skill_timer::instance();
    if (!timer.is_skill_allowed(*ch, SKILL_STOMP)) {
        send_to_char("You cannot use this skill yet.\r\n", ch);
        return;
    }

    olog_hai::do_sanctuary_check(ch);
    send_to_char("You jump into the air and slam down feet first onto the ground!\n\r", ch);
    olog_hai::room_target(ch, &olog_hai::apply_stomp_affect);
    timer.add_skill_timer(*ch, SKILL_STOMP, STOMP_TIMER);

    // Stun the Olog-hai after using the skill
    int wait_delay = number(8, PULSE_VIOLENCE);
    WAIT_STATE_FULL(ch, wait_delay, 0, 0, 59, 0, 0, 0, AFF_WAITING, TARGET_NONE);
}
