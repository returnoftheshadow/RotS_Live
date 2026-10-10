#ifndef MOB_PROGS_BANKER_H
#define MOB_PROGS_BANKER_H

/* Banker mob program (program 34): each account's vault of items and coins,
 * one per side, the same at every banker on that side. */

#include "shopkeeper.h"

#include "../interpre.h"
#include "../objects_json.h"

#include <ctime>
#include <functional>
#include <string>
#include <vector>

constexpr int PROG_BANKER = 34;
constexpr int BANKER_FEE_MAX = 10000; /* copper per item per day */
constexpr int BANKER_MAXDAYS_MAX = 365;
constexpr int BANKER_MARKUP_DEFAULT = 30; /* racial_markup=yes */
constexpr int BANKER_MARKUP_MAX = 3000;

enum { BANK_SIDE_NONE = 0,
    BANK_SIDE_LIGHT = 1,
    BANK_SIDE_DARK = 2,
    BANK_SIDE_THIRD = 3 };

/* Banker settings: "hours=<windows>", "fee=<copper>", "maxdays=<n>",
 * "racial_markup=yes|<percent>", "greeting=<message>",
 * "greeting_other=<message>", "attacks=yes|no". All optional. */
constexpr size_t BANKER_GREETING_MAX = 78;
struct banker_config {
    bool ok = true; /* false: the banker does no business */
    std::vector<vendor_hours_window> hours; /* empty = always open */
    int fee = 0; /* copper per item per day; 0 = free */
    int maxdays = 0;
    int markup = 0; /* percent added for another race; 0 = none */
    bool fee_given = false; /* a fee= line was present, good or bad */
    std::string greeting; /* shown on balance in place of the default line */
    std::string greeting_other; /* the same, for a customer of another race */
    bool attacks = true; /* false: the mob AI never runs, so the banker never starts a fight */
};

banker_config parse_banker_options(const char* text, std::vector<vendor_problem>* problems);

/* BANK_SIDE_NONE for any race with no vault (immortals, NPC-only races). */
int bank_side_for_race(int race);
const char* bank_side_file_name(int side); /* nullptr for no side */

/* How many day-start points (start_hour_utc, the hour of the daily reboot,
 * in UTC) lie between the two times. Never negative. */
int bank_days_stored(time_t deposited, time_t now, int start_hour_utc);

/* Copper to withdraw one slot holding `items` objects after `days` days. */
long long bank_fee(const banker_config& config, int days, int items, bool other_race);

constexpr int BANK_VAULT_SCHEMA_VERSION = 1;

/* One stored item. A container's contents follow it in objects, in the order
 * the rent save walks them, with wear_pos holding the nesting depth (0 for
 * the item itself). */
struct bank_slot {
    long deposited = 0; /* real time of the deposit */
    std::vector<objects_json::ObjectRecord> objects;
};

struct bank_vault {
    int coins = 0; /* copper */
    std::vector<bank_slot> slots;
    bool readable = true; /* false: the file on disk could not be read */
    /* Runtime only: the last write of this vault failed, so the file may hold
     * something this copy no longer does. The next open writes it again. */
    bool file_behind = false;
    std::string read_error; /* why not, as last logged; never stored */
};

std::string serialize_bank_vault(const bank_vault& vault);
/* On failure *vault is left untouched and *error says why. */
bool deserialize_bank_vault(const std::string& json, bank_vault* vault, std::string* error);

struct char_data;
struct obj_data;
struct waiting_type;

/* The three things the bank asks the game for, replaceable by tests. Passing
 * an empty function restores the game behaviour. */
void bank_set_directory_resolver(std::function<std::string(const std::string& account_name)> resolver);
/* The saver is told whether the object file matters to the caller, and answers false if the save failed. */
void bank_set_character_saver(std::function<bool(struct char_data*, bool objects_too)> saver);
void bank_set_clock(std::function<time_t()> clock);
time_t bank_now();
/* False: the character file, or (unless objects_too is false) its object file, was not saved. */
/* *character_written (when given): the character file itself was written,
 * whatever happened to the object file. A stand-in saver (tests) reports it
 * by setting ch->specials.saved_character_file; otherwise its answer counts. */
bool bank_save_character(struct char_data* ch, bool objects_too = true, bool* character_written = nullptr);

/* The vault table holds the ONLY in-memory copy of each vault; nothing else
 * may read or write a vault file. bank_vault_open returns nullptr (with
 * *error set) when the account has no folder, the side is not 1-3, or the
 * file on disk could not be read. An unreadable file is never overwritten;
 * it is read again on the next open, so a repaired file works without a reboot. */
bank_vault* bank_vault_open(const std::string& account_name, int side, std::string* error);
/* Writes the in-memory copy to its file: temp file, then rename. */
bool bank_vault_write(const std::string& account_name, int side, std::string* error);
/* Writes again every vault whose last write failed; before a shutdown or reboot. */
void bank_vaults_write_behind();
#ifdef TESTING
/* Drops the table. Never in the game: the table is the only right copy of a
 * vault whose last write failed (bank_vaults_write_behind). */
void bank_vault_forget_all_for_tests();
#endif

/* Program 34. Mirrors the barter vendor's registry: configs are parsed at
 * boot and when a builder saves the mob, and kept by mob rnum. */
int banker(struct char_data* host, struct char_data* ch, int cmd, char* arg, int callflag, struct waiting_type* wtl);
void banker_config_boot(); /* all program-34 prototypes */
void banker_config_rebuild(int mob_rnum, struct char_data* builder, bool report = true);
bool is_banker_candidate(const struct char_data* proto, int rnum);
void banker_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder); /* report only */
void banker_implement_check(int mob_rnum, struct char_data* builder); /* /imp only: soft reminders */
const banker_config* banker_config_for(int mob_rnum); /* nullptr if none */

/* Stored objects. Records mirror the rent save's, with depth in wear_pos. */
void bank_records_from_obj(struct obj_data* obj, std::vector<objects_json::ObjectRecord>* out);
/* A slot rebuilt the way the rent load does it: what still exists is built,
 * what is gone is skipped and named in `missing`. Normally one object in
 * `tops` (the item with its contents); when a container is gone its contents
 * move up to the nearest surviving container, or into `tops` as loose items.
 * Wands and staves keep their stored charges. All in no room and on nobody. */
struct bank_rebuild {
    std::vector<struct obj_data*> tops;
    std::vector<int> missing; /* vnums with no prototype any more */
    bool top_missing = false; /* the stored item itself is gone */
    bool bad_nesting = false; /* impossible depths: nothing was built */
};
bank_rebuild bank_rebuild_records(const std::vector<objects_json::ObjectRecord>& records);
/* The whole item or nothing: nullptr (and nothing left behind) if anything is missing. */
struct obj_data* bank_obj_from_records(const std::vector<objects_json::ObjectRecord>& records);
bool bank_obj_storable(struct obj_data* obj); /* false if it, or anything inside it, can't be rented */
const char* bank_slot_name(const bank_slot& slot); /* short description of the stored item */

struct bank_balance_row {
    std::string name;
    int inside; /* objects inside a container; < 0 when it is not one */
    std::string fee; /* already worded; unused when the fee column is off */
};
std::string format_bank_balance(const std::string& coins, int coin_limit_gold, int slots_used, int slots_max,
    const std::vector<bank_balance_row>& rows, bool show_fee);

/* The immortal vault command: view any vault, and move items or coins in
 * and out of one. Works through the same vault table as the bankers. */
ACMD(do_vault);

struct bank_account_ref {
    std::string name;
    std::string email;
};
/* How `vault` finds an account, replaceable by tests; empty functions
 * restore the game lookups. */
void bank_set_account_lookups(
    std::function<bool(const std::string& identifier, bank_account_ref* out)> by_email_or_name,
    std::function<bool(const std::string& exact_name, bank_account_ref* out)> by_name,
    std::function<bool(const std::string& character, bank_account_ref* out, int* race)> by_character);
std::string format_vault_view(const bank_account_ref& account, const std::string& character, int side,
    const bank_vault& vault);

#endif
