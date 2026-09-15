/* ************************************************************************
 *   File: limits.h                                      Part of CircleMUD *
 *  Usage: header file: protoypes of functions in limits.c                 *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef LIMITS_H
#define LIMITS_H

struct char_data;
struct affected_type;

/* Public Procedures */
float mana_gain(const char_data* ch);
float get_bonus_mana_gain(const char_data* character);

float hit_gain(const char_data* ch);
float get_bonus_hit_gain(const char_data* character);

float move_gain(const char_data* ch);
float get_bonus_move_gain(const char_data* character);
// int spirit_gain(const char_data* ch);

int xp_to_level(int level);

void do_start(struct char_data* ch);
void finalize_new_character_start_state(struct char_data* ch);
void set_title(struct char_data* ch);
/* The most experience a single award can bank. */
const int constexpr MAX_EXP_PER_GAIN = 7000;

/*
 * How much of a prospective award would actually be banked by gain_exp.
 * Callers that announce an award to the player should report this, not
 * the raw amount, so the number shown matches the experience received.
 */
int cap_exp_gain(int gain);
void gain_exp(struct char_data* ch, int gain);
void gain_exp_regardless(struct char_data* ch, int gain);
void gain_condition(struct char_data* ch, int condition, int value);
int check_idling(struct char_data* ch);
// returns non-zero if ch was extracted
void point_update(void);
void update_pos(struct char_data* victim);
void remove_fame_war_bonuses(struct char_data* ch, struct affected_type* pkaff);

struct title_type {
    char* title_m;
    char* title_f;
    int exp;
};

#endif /* LIMITS_H */
