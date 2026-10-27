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
inline constexpr int AFFECT_TOTAL_UPDATE = 3;
inline constexpr int AFFECT_TOTAL_SET = 1;
inline constexpr int AFFECT_TOTAL_REMOVE = 2;
inline constexpr int AFFECT_TOTAL_TIME = 4;

inline constexpr int AFFECT_MODIFY_SET = 1;
inline constexpr int AFFECT_MODIFY_REMOVE = 0;
inline constexpr int AFFECT_MODIFY_TIME = 2;

void affect_total_room(const struct room_data* room, int mode = AFFECT_TOTAL_UPDATE);
void affect_modify_room(struct room_data* room, byte loc, int mod, long bitv, char add);
void affect_to_room(struct room_data* room, const struct affected_type* af);
void affect_remove_room(struct room_data* room, struct affected_type* af);
void affect_from_room(struct room_data* room, byte skill);

void affect_total(struct char_data* ch, int mode = AFFECT_TOTAL_UPDATE);
void affect_modify(struct char_data* ch, byte loc, int mod, long bitv, char add, sh_int counter);
void affect_to_char(struct char_data* ch, const struct affected_type* af);
/* Bound-checked lookup into spell_wear_off_msg[]. That table is indexed by spell number but is
   far shorter than MAX_SKILLS, so an unchecked index reads past its end. Returns NULL when the
   spell number has no entry at all, otherwise the entry (which may be the empty string). */
const char* spell_wear_off_message(int spell_number);

void affect_remove_notify(struct char_data*, struct affected_type*);
void affect_remove(struct char_data* ch, struct affected_type* af);
void affect_from_char_notify(struct char_data*, byte);
void affect_from_char(struct char_data* ch, byte skill);

#include <stddef.h>

class death_watch;

affected_type* affected_by_spell(const char_data* character, byte skill, affected_type* firstaf = 0);
affected_type* removeable_spell_affection(const char_data* character, const affected_type* aff, affected_type* start_affect);
affected_type* room_affected_by_spell(const room_data* room, int spell);

void affect_join(struct char_data* ch, struct affected_type* af,
    char avg_dur, char avg_mod);

/* utility */
struct obj_data* create_money(int amount);
int isname(const char* str, const char* namelist, char full = 1);
char* fname(const char* namelist);

/* ******** objects *********** */

void obj_to_char(struct obj_data* object, struct char_data* ch);
void obj_from_char(struct obj_data* object);

/* How equip_char and unequip_char treat an item's spells (APPLY_SPELL) and its poison. Its stat
 * affects are given or taken either way. */
enum class gear_change {
    /* Someone puts the item on or takes it off: its spells are cast on the wearer, and its
     * poison hurts the wearer. */
    cast,
    /* The game dresses or strips someone coming into or leaving the game (login, rent, quit,
     * removal): only a spell that gives a lasting affect for as long as the item is worn
     * (item_spell_is_worn_affect) gives or takes it. No other spell is cast and the poison hurts
     * nobody, so the wearer and the item are never freed. A login casts the rest once the
     * character stands in its room (cast_quietly_worn_gear). */
    quiet,
};

/* Puts <obj>, held by nobody, on <ch> in <pos>. With gear_change::cast, putting it on can hurt
 * the wearer: a spell the item casts on its wearer, or the poison it gives (5 damage, and death
 * at 0 hit points or fewer). The blow can kill the wearer or set off a script that removes the
 * wearer or the item. Returns false when the wearer or the item was freed: the caller must touch
 * neither again, and one that must know which watches it with a freed_watch. Returns true
 * otherwise, whether or not the item is worn: an item refused for its alignment or race goes into
 * the inventory, and one a death stripped off lies in the corpse. */
bool equip_char(struct char_data* ch, struct obj_data* obj, int pos,
    gear_change change = gear_change::cast);
/* Takes the item in <pos> off <ch> and returns it; logs and returns null when nothing is worn
 * there. The item is left for the caller to put somewhere. With gear_change::cast, taking it off
 * can hurt the wearer as putting it on can (equip_char), unless the wearer is dead or in no
 * room, so it can free the wearer: a caller that uses the wearer afterwards watches it with a
 * freed_watch, and puts the item where the wearer was if it is gone. The item a freed wearer
 * took off is held by nobody. */
struct obj_data* unequip_char(struct char_data* ch, int pos,
    gear_change change = gear_change::cast);
/* Casts on <wearer> what putting its gear on quietly (gear_change::quiet) passed over, as putting
 * each item on with gear_change::cast does: every item spell (APPLY_SPELL) that is not a worn
 * affect, in wear-slot order, and then the poison's blow (equip_char) once, if <wearer> was not
 * poisoned before its gear went on (<poisoned_before_gear>) and is poisoned now. The worn affects
 * are not cast again. Call it once, after gear put on quietly (a login), when <wearer> stands in
 * its room. A cast can kill the wearer or anyone else in the room, or set off a script that
 * removes someone: an item that is freed or stripped off casts nothing more, and the other items
 * still cast. Nothing more is cast, the poison's blow included, once <stop_at_death> (null for
 * none) reports a death. Returns false when <wearer> was freed: the caller must not touch it
 * again. A null <wearer> is logged and nothing is cast. */
bool cast_quietly_worn_gear(struct char_data* wearer, bool poisoned_before_gear,
    const death_watch* stop_at_death = nullptr);
/* Encumbrance weight an item of this weight adds when worn in this slot. */
int worn_encumb_weight(int weight, int slot);

struct obj_data* get_obj_in_list(const char* name, struct obj_data* list);
struct obj_data* get_obj_in_list_num(int num, struct obj_data* list);
struct obj_data* get_obj_in_list_vnum(int vnum, struct obj_data* list);
struct obj_data* get_obj_in_list_num_containers(int num, struct obj_data* list);
int count_obj_in_list(int num, struct obj_data* list);
/* returns the number of objects with this virtual number.
    if num == 0, returns the total number of objects */

struct obj_data* get_obj(const char* name);
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

struct char_data* get_char_room(const char* name, int room);
struct char_data* get_char_num(int nr);
struct char_data* get_char(const char* name);

void char_from_room(struct char_data* ch);
void char_to_room(struct char_data* ch, int room);
void extract_char(struct char_data* ch, int new_room = -1);

/* Lends a character a room it does not stand in, for code that asks where it is (the procedures
 * on the far side of a door it knocks on), while it stays on its own room's list of people.
 * char_from_room and extract_char first end any lend of the character they are given, putting it
 * back where it stands, so that it leaves the right room; otherwise the lend puts it back when it
 * goes out of scope. A lend must be a local variable: lends nest, and each ends in the reverse
 * order of its making. */
class room_lend {
public:
    /* Lends <character>, which stands in a room, the room <lent_room>. */
    room_lend(char_data* character, int lent_room);
    ~room_lend();

    room_lend(const room_lend&) = delete;
    room_lend& operator=(const room_lend&) = delete;

    /* Ends every lend of <character>, putting it back in the room it stands in. */
    static void end_for(char_data* character);

private:
    // The newest lend still in scope; each points on to the one made before it. Null when nothing
    // is lent, which is nearly always, so char_from_room pays one test.
    static room_lend* newest;

    // The character lent the room; null once the lend has ended.
    char_data* m_character;
    // The room the character stands in, given back when the lend ends.
    int m_own_room;
    // The lend made before this one, which is the newest again once this one goes.
    room_lend* m_older;
};

int in_affected_list(const struct char_data* ch);

/* find if character can see */
int keyword_matches_char(const struct char_data*, const struct char_data*, const char*);
struct char_data* get_char_room_vis(struct char_data* ch, const char* name,
    int dark_ok = 0);
struct char_data* get_player_vis(struct char_data* ch, const char* name);
struct char_data* get_char_vis(struct char_data* ch, const char* name,
    int dark_ok = 0);
/* only_type: count and return only items of this type (-1: any type). */
struct obj_data* get_obj_in_list_vis(const struct char_data* ch, const char* name,
    struct obj_data* list, int num, int only_type = -1);
struct obj_data* get_obj_vis(const struct char_data* ch, const char* name);
struct obj_data* get_object_in_equip_vis(const struct char_data* ch,
    const char* arg, struct obj_data* equipment[], int* j);

/* Splits "N.name": returns N (1 with no number, 0 if N is not a number)
 * and leaves just the keyword in *name. */
int get_number(char** name);
/* A container named by arg for put, get, look and examine: generic_find
 * with bits and FIND_OBJ_BELT_KEYRING. */
int find_container(struct char_data* ch, const char* arg, int bits,
    struct char_data** tar_ch, struct obj_data** tar_obj);
/* The keyring on this belt slot if it answers to name by any keyword
 * (nullptr name: any keyring), else nullptr. */
struct obj_data* belt_keyring(struct char_data* ch, int slot, const char* name);
/* A keyring is always open and has no lock: for a keyring, tells ch so and
 * returns true; false for anything else. */
enum class keyring_action { open_close,
    lock };
bool refuse_keyring(struct char_data* ch, const struct obj_data* obj, keyring_action action);

void add_follower(struct char_data* ch, struct char_data* leader, int mode);
void stop_follower(struct char_data* ch, int mode);
char circle_follow(const struct char_data* ch, struct char_data* victim, int mode);
void die_follower(struct char_data* ch);
/* True if <entry> is on <leader>'s follower list and still stands for <follower>. <entry> and
 * <follower> are compared by address until <entry> is found there, so either may have gone (a
 * null one is never found). */
bool is_follower_entry(const char_data* leader, const follow_type* entry, const char_data* follower);

inline constexpr int FOLLOW_MOVE = 1;
inline constexpr int FOLLOW_GROUP = 2;
inline constexpr int FOLLOW_REFOL = 3;

/* find all dots */

int find_all_dots(char* arg);

inline constexpr int FIND_INDIV = 0;
inline constexpr int FIND_ALL = 1;
inline constexpr int FIND_ALLDOT = 2;

/* Generic Find */

int generic_find(const char* arg, int bitvector, struct char_data* ch,
    struct char_data** tar_ch, struct obj_data** tar_obj);

inline constexpr int FIND_CHAR_ROOM = 1;
inline constexpr int FIND_CHAR_WORLD = 2;
inline constexpr int FIND_OBJ_INV = 4;
inline constexpr int FIND_OBJ_ROOM = 8;
inline constexpr int FIND_OBJ_WORLD = 16;

inline constexpr int FIND_OBJ_EQUIP = 32;
/* Belt keyrings come last (put, get, look in, examine): the worn-gear step
 * passes over keyrings on the belt slots, and after the room they are
 * searched by any keyword, "N.name" numbering on after the longer of the
 * inventory and room match lists. A belt keyring found there returns
 * FIND_OBJ_BELT_KEYRING (its weight is already carried, as for FIND_OBJ_INV). */
inline constexpr int FIND_OBJ_BELT_KEYRING = 64;

/* prototypes from crash save system */

int Crash_get_filename(const char* orig_name, char* filename);
int Crash_delete_file(const char* name);
int Crash_delete_crashfile(const struct char_data* ch);
int Crash_clean_file(const char* name);
void Crash_listrent(const struct char_data* ch, const char* name);
// int	Crash_load(struct char_data *ch);
void Crash_crashsave(struct char_data* ch, int rent_code = RENT_CRASH);
void Crash_idlesave(struct char_data* ch);
void Crash_save_all(void);
FILE* Crash_get_file_by_name(const char* name, const char* mode);
FILE* Crash_load(struct char_data* ch);
/* Brings <ch>, just read from its player file, into the game with its gear, its aliases and its
 * followers. Everything is read first. Then <ch> enters its room and its gear's spells are cast
 * (cast_quietly_worn_gear), and then each follower in turn enters the room <ch> stands in, its
 * gear's spells are cast, and it follows <ch>. A death of <ch> stops all casting, so a login kills
 * it at most once. Returns false when a cast freed <ch>: the caller must not touch it again. */
bool load_character(struct char_data* ch);
/* What a follower record's flag_config says the follower was when it was saved. */
#define FOL_MOUNT 0
#define FOL_ORC_FRIEND 1
#define FOL_TAMED 2
#define FOL_GUARDIAN 3
void stage_account_backed_object_bytes_for_character(const struct char_data* ch, const char* bytes, size_t length);
void clear_account_backed_object_bytes_for_character(const struct char_data* ch);

/* prototypes from fight.c */
void set_fighting(struct char_data* ch, struct char_data* victim);
void stop_fighting(struct char_data* ch);
void stop_follower(struct char_data* ch);
void hit(struct char_data* ch, struct char_data* victim, int type);
void forget(struct char_data* ch, const struct char_data* victim);
void remember(struct char_data* ch, struct char_data* victim);
int damage(struct char_data* ch, struct char_data* victim, int dam, int attacktype, int hit_location);
int check_sanctuary(char_data* ch, char_data* victim);

char* money_message(int sum, int mode = 0);

int char_exists(int num);
void set_char_exists(int num);
void remove_char_exists(int num);
int register_npc_char(struct char_data*);
int register_pc_char(struct char_data*);

int can_swim(const struct char_data* ch);

void stop_riding(struct char_data* ch);
void stop_riding_all(const struct char_data* mount); /*emergency dismount */
int char_power(int lev);
void recalc_zone_power();
int report_zone_power(const char_data* ch);

#endif /* HANDLER_H */
