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

#include "caster_snapshot.h" /* For the caster_snapshot parameters below */
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
// Adds `af` to `room` exactly as the two-argument form does, and records
// `caster` as its source. Only a ROOMAFF_SPELL affect carries a caster; any
// other `af->type` is added with no record.
void affect_to_room(struct room_data* room, struct affected_type* af, const caster_snapshot& caster);
// The caster recorded for the live ROOMAFF_SPELL affect at (room, spell), or
// null when none was recorded. The returned pointer stays valid only until the
// next affect_remove_room() for that (room, spell); copy the caster_snapshot
// if it must outlive that call.
const caster_snapshot* room_affect_caster(const room_data* room, int spell);
// Records `caster` as the source of the ROOMAFF_SPELL affect at (room, spell),
// replacing any caster already recorded for it.
void set_room_affect_caster(room_data* room, int spell, const caster_snapshot& caster);
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
// Returns the first affect of `affect_type` on `character`, or nullptr. Unlike affected_by_spell,
// it walks the whole list rather than stopping after MAX_AFFECT entries.
affected_type* get_affect_unbounded(const char_data* character, int affect_type);
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
// Same answer as the live form, from a caster's cast-time snapshot.
int other_side(const caster_snapshot& character, const char_data* other);
// The race half of other_side(): 1 when the two races are on opposite sides
// of the race war. RACE_GOD is on every side. Unlike other_side(), this never
// exempts an NPC, so it can place a mob by its race.
int other_side_race(int character_race, int other_race);
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
// damage() with the kill credit named separately from the character that
// engages the victim. `ch` engages exactly as damage() does; `credited_killer`
// (which may be null, may equal `ch`, and may stand in another room) is what
// reaches die(), and is never engaged.
int damage_credited(struct char_data* ch, struct char_data* victim, struct char_data* credited_killer, int dam, int attacktype, int hit_location);
// The live character recorded as the source of `victim`'s poison, or null when
// the record no longer names a live character. Never dereferences the recorded
// pointer, so an extracted or slot-recycled poisoner is reported as null rather
// than dangling.
struct char_data* resolve_poisoner(const struct char_data& victim);
// Records `poisoner` as the source of `victim`'s poison, for resolve_poisoner()
// to read back. A null `poisoner` -- a poisoned meal or drink has no character
// behind it -- clears the record rather than leaving it half-set. `poisoner` is
// not retained: only its identity is stored, and it is never dereferenced later.
void record_poison_origin(struct char_data* victim, struct char_data* poisoner);
// Applies poison from eaten or drunk food, which has no recorded poisoner. Only the stronger
// poison stays on `victim`: a consumed poison no longer than the one already running changes
// nothing (and the running poison keeps its poisoner); a longer one replaces it and clears the
// record, since nobody owns it.
void apply_consumed_poison(struct char_data* victim, const struct affected_type& poison);

// Punishment class for a PC death, chosen by classify_pc_death().
enum class death_punishment {
    legacy, // die()/raw_kill() decide from the credited killer, as before
    mob_death, // punished as a death to a mob, whatever landed the blow
    player_death, // punished as a player kill, whatever landed the blow
};

// A mob acting for itself: an NPC that is neither MOB_PET nor MOB_ORC_FRIEND.
// Null and players answer false.
bool is_real_mob(const struct char_data* character);

// Punishment class for a player character's death. `self_inflicted` is the tick shape
// (attacker == victim: a room-affect tick, a poison tick, starvation, a fall); `credited` says
// whether anybody was credited before the engaged-opponent fallback. A self-inflicted poison
// death is decided by engagement with a real mob. Any other self-inflicted death that credits
// nobody (a tick whose caster no longer resolves, a builder-placed affect) takes the gentle
// arm the historical self-credit gave it. Everything else, including every direct hit, is
// legacy: the credited killer decides.
death_punishment classify_pc_death(int attack_type, bool self_inflicted, bool credited, bool engaged_with_real_mob);
// Whether an uncredited death may be credited to whoever the victim was fighting. False only
// for the uncredited non-poison tick above, so its death record still names the victim's
// contributors instead of turning the engaged mob into the killer. It answers for
// player-character victims; an NPC victim always falls back.
bool death_credit_falls_back_to_opponent(int attack_type, bool self_inflicted, bool credited);

// The real mob counted as engaged with `victim` at the instant of death, or
// null. `engaged_opponent` must be captured before stop_fighting() runs;
// engagement ignores visibility.
struct char_data* find_engaged_real_mob(struct char_data* victim, struct char_data* engaged_opponent);

// Whether the death takes the full mob-death XP loss on top of the
// unconditional tenth already applied.
bool death_takes_full_mob_xp_loss(const struct char_data* killer, death_punishment punishment);

// True selects the gentle player-kill penalty over the harsh one.
bool death_counts_as_player_kill(const struct char_data* killer, death_punishment punishment);

// Whether the corpse pulls wearables and keys out of the dead character's containers so
// they cannot be hidden from looters. A player-kill punishment strips, a mob-death
// punishment never does, and the legacy class keeps the historical rule: a poison death
// or any killer that is not an NPC (including no killer at all) strips.
bool death_strips_corpse_containers(const struct char_data* killer, int attack_type, death_punishment punishment);

// Whether a PC death writes EXPLOIT_DEATH entries naming its player contributors: true for a
// player's killing blow and for an uncredited death (null killer), false when a mob's blow
// keeps the legacy mob-death-only record.
bool death_names_player_contributors(const struct char_data* killer);

// The NPC an EXPLOIT_MOBDEATH record names, or null when no record is due.
// legacy names a real-mob killer only; mob_death names the killer when it is
// a real mob, else engaged_mob; player_death suppresses the record.
struct char_data* mobdeath_record_mob(struct char_data* killer, struct char_data* engaged_mob, death_punishment punishment);

int check_sanctuary(char_data* ch, char_data* victim);

char* money_message(int sum, int mode = 0);

int char_exists(int num);
// Marks slot `num` occupied without naming an owner: char_exists() then reports
// the slot live while char_by_abs_number() still answers null. Production
// registration uses the two-argument form below.
void set_char_exists(int num);
// Registers `ch` as the current owner of slot `num` and gives it a fresh
// ch->registration_serial, so an identity captured under an earlier
// registration of the same slot no longer resolves. An out-of-range `num` is
// ignored, as it is by set_char_exists(num) and remove_char_exists().
void set_char_exists(int num, struct char_data* ch);
void remove_char_exists(int num);
// The character currently registered under abs_number `num`, or null when the
// slot is free or was never given an owner. The sanctioned way to turn a
// recorded abs_number back into a character without dereferencing a possibly
// stale pointer.
struct char_data* char_by_abs_number(int num);
// Whether `character` is in the game world: in a room. A player who quit sits at the
// character menu with its registration intact until the socket closes, but extract_char()
// took it out of its room first; a linkless body and a respawned player keep their room.
// Credit and attribution resolvers require this, so a parked character is never credited.
bool character_in_game(const struct char_data* character);
int register_npc_char(struct char_data*);
int register_pc_char(struct char_data*);

int can_swim(struct char_data* ch);

void stop_riding(struct char_data* ch);
void stop_riding_all(struct char_data* mount); /*emergency dismount */
int char_power(int lev);
void recalc_zone_power();
int report_zone_power(char_data* ch);

#endif /* HANDLER_H */
