/* ************************************************************************
 *   File: handler.h                                     Part of CircleMUD *
 *  Usage: header file: prototypes of handling and utility functions       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef HANDLER_H
#define HANDLER_H

#include "platdef.h" /* For sh_int, ush_int, byte, etc. */
#include "structs.h" /* For the RENT_CRASH macro */

/* handling the affected-structures */
#define AFFECT_TOTAL_UPDATE 3
#define AFFECT_TOTAL_SET 1
#define AFFECT_TOTAL_REMOVE 2
#define AFFECT_TOTAL_TIME 4

#define AFFECT_MODIFY_SET 1
#define AFFECT_MODIFY_REMOVE 0
#define AFFECT_MODIFY_TIME 2

void affect_total_room(struct room_data* room, int mode = AFFECT_TOTAL_UPDATE);
void affect_modify_room(struct room_data* room, byte loc, int mod, long bitv, char add);
void affect_to_room(struct room_data* room, struct affected_type* af);
void affect_remove_room(struct room_data* room, struct affected_type* af);
void affect_from_room(struct room_data* room, byte skill);

void affect_total(struct char_data* ch, int mode = AFFECT_TOTAL_UPDATE);
void affect_modify(struct char_data* ch, byte loc, int mod, long bitv, char add, sh_int counter);
void affect_to_char(struct char_data* ch, struct affected_type* af);
void affect_remove_notify(struct char_data*, struct affected_type*);
void affect_remove(struct char_data* ch, struct affected_type* af);
void affect_from_char_notify(struct char_data*, byte);
void affect_from_char(struct char_data* ch, byte skill);

#include <stddef.h>

affected_type* affected_by_spell(const char_data* character, byte skill, affected_type* firstaf = 0);
affected_type* room_affected_by_spell(const room_data* room, int spell);

void affect_join(struct char_data* ch, struct affected_type* af,
    char avg_dur, char avg_mod);

/* utility */
struct obj_data* create_money(int amount);
int isname(const char* str, const char* namelist, char full = 1);
char* fname(char* namelist);

/* ******** objects *********** */

void obj_to_char(struct obj_data* object, struct char_data* ch);
void obj_from_char(struct obj_data* object);

void equip_char(struct char_data* ch, struct obj_data* obj, int pos);
struct obj_data* unequip_char(struct char_data* ch, int pos);

struct obj_data* get_obj_in_list(char* name, struct obj_data* list);
struct obj_data* get_obj_in_list_num(int num, struct obj_data* list);
struct obj_data* get_obj_in_list_vnum(int vnum, struct obj_data* list);
struct obj_data* get_obj_in_list_num_containers(int num, struct obj_data* list);
int count_obj_in_list(int num, struct obj_data* list);
/* returns the number of objects with this virtual number.
    if num == 0, returns the total number of objects */

struct obj_data* get_obj(char* name);
struct obj_data* get_obj_num(int nr);

void obj_to_room(struct obj_data* object, int room);
void obj_from_room(struct obj_data* object);
void obj_to_obj(struct obj_data* obj, struct obj_data* obj_to, char change_weight = 1);
void obj_from_obj(struct obj_data* obj);
void object_list_new_owner(struct obj_data* list, struct char_data* ch);

void extract_obj(struct obj_data* obj);

/* ******* characters ********* */
int other_side(const char_data* character, const char_data* other);
int other_side_num(int ch_race, int i_race);

struct char_data* get_char_room(char* name, int room);
struct char_data* get_char_num(int nr);
struct char_data* get_char(char* name);

void char_from_room(struct char_data* ch);
void char_to_room(struct char_data* ch, int room);
void extract_char(struct char_data* ch, int new_room = -1);

int in_affected_list(struct char_data* ch);

/* find if character can see */
int keyword_matches_char(struct char_data*, struct char_data*, char*);
struct char_data* get_char_room_vis(struct char_data* ch, char* name,
    int dark_ok = 0);
struct char_data* get_player_vis(struct char_data* ch, char* name);
struct char_data* get_char_vis(struct char_data* ch, char* name,
    int dark_ok = 0);
struct obj_data* get_obj_in_list_vis(struct char_data* ch, char* name,
    struct obj_data* list, int num);
struct obj_data* get_obj_vis(struct char_data* ch, char* name);
struct obj_data* get_object_in_equip_vis(struct char_data* ch,
    char* arg, struct obj_data* equipment[], int* j);

void add_follower(struct char_data* ch, struct char_data* leader, int mode);
void stop_follower(struct char_data* ch, int mode);
char circle_follow(struct char_data* ch, struct char_data* victim, int mode);
void die_follower(struct char_data* ch);

#define FOLLOW_MOVE 1
#define FOLLOW_GROUP 2
#define FOLLOW_REFOL 3

/* find all dots */

int find_all_dots(char* arg);

#define FIND_INDIV 0
#define FIND_ALL 1
#define FIND_ALLDOT 2

/* Generic Find */

int generic_find(char* arg, int bitvector, struct char_data* ch,
    struct char_data** tar_ch, struct obj_data** tar_obj);

#define FIND_CHAR_ROOM 1
#define FIND_CHAR_WORLD 2
#define FIND_OBJ_INV 4
#define FIND_OBJ_ROOM 8
#define FIND_OBJ_WORLD 16

#define FIND_OBJ_EQUIP 32

/* prototypes from crash save system */

int Crash_get_filename(char* orig_name, char* filename);
int Crash_delete_file(char* name);
int Crash_delete_crashfile(struct char_data* ch);
int Crash_clean_file(char* name);
void Crash_listrent(struct char_data* ch, char* name);
// int	Crash_load(struct char_data *ch);
void Crash_crashsave(struct char_data* ch, int rent_code = RENT_CRASH);
void Crash_idlesave(struct char_data* ch);
void Crash_save_all(void);
FILE* Crash_get_file_by_name(char* name, char* mode);
FILE* Crash_load(struct char_data* ch);
void stage_account_backed_object_bytes_for_character(const struct char_data* ch, const char* bytes, size_t length);
void clear_account_backed_object_bytes_for_character(const struct char_data* ch);

/* prototypes from fight.c */
void set_fighting(struct char_data* ch, struct char_data* victim);
void stop_fighting(struct char_data* ch);
void stop_follower(struct char_data* ch);
void hit(struct char_data* ch, struct char_data* victim, int type);
void forget(struct char_data* ch, struct char_data* victim);
void remember(struct char_data* ch, struct char_data* victim);
int damage(struct char_data* ch, struct char_data* victim, int dam, int attacktype, int hit_location);
int check_sanctuary(char_data* ch, char_data* victim);

char* money_message(int sum, int mode = 0);

int char_exists(int num);
void set_char_exists(int num);
void remove_char_exists(int num);
int register_npc_char(struct char_data*);
int register_pc_char(struct char_data*);

int can_swim(struct char_data* ch);

void stop_riding(struct char_data* ch);
void stop_riding_all(struct char_data* mount); /*emergency dismount */
int char_power(int lev);
void recalc_zone_power();
int report_zone_power(char_data* ch);

#endif /* HANDLER_H */
