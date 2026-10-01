#ifndef MOB_PROGS_BANKER_H
#define MOB_PROGS_BANKER_H

/* Banker mob program (program 34): each account's vault of items and coins,
 * one per side, the same at every banker on that side. */

#include "shopkeeper.h"

#include "../objects_json.h"

#include <ctime>
#include <functional>
#include <string>
#include <vector>

constexpr int PROG_BANKER = 34;
constexpr int BANKER_FEE_MAX = 10000; /* copper per item per day */
constexpr int BANKER_MAXDAYS_MAX = 365;
constexpr int BANKER_MARKUP_DEFAULT = 30; /* racial_markup=yes */
constexpr int BANKER_MARKUP_MAX = 300;

enum { BANK_SIDE_NONE = 0,
    BANK_SIDE_LIGHT = 1,
    BANK_SIDE_DARK = 2,
    BANK_SIDE_THIRD = 3 };

/* Banker settings: "hours=<windows>", "fee=<copper>", "maxdays=<n>",
 * "racial_markup=yes|<percent>". All optional. */
struct banker_config {
    bool ok = true; /* false: the banker does no business */
    std::vector<vendor_hours_window> hours; /* empty = always open */
    int fee = 0; /* copper per item per day; 0 = free */
    int maxdays = 0;
    int markup = 0; /* percent added for another race; 0 = none */
};

banker_config parse_banker_options(const char* text, std::vector<vendor_problem>* problems);

/* BANK_SIDE_NONE for any race with no vault (immortals, NPC-only races). */
int bank_side_for_race(int race);
const char* bank_side_file_name(int side); /* nullptr for no side */

/* How many day-start points (start_hour, server local time) lie between the
 * two times. Never negative. */
int bank_days_stored(time_t deposited, time_t now, int start_hour);

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
void bank_set_character_saver(std::function<void(struct char_data*)> saver);
void bank_set_clock(std::function<time_t()> clock);
time_t bank_now();
void bank_save_character(struct char_data* ch);

/* The vault table holds the ONLY in-memory copy of each vault; nothing else
 * may read or write a vault file. bank_vault_open returns nullptr (with
 * *error set) when the account has no folder, the side is not 1-3, or the
 * file on disk could not be read. An unreadable file is never overwritten. */
bank_vault* bank_vault_open(const std::string& account_name, int side, std::string* error);
/* Writes the in-memory copy to its file: temp file, then rename. */
bool bank_vault_write(const std::string& account_name, int side, std::string* error);
void bank_vault_forget_all(); /* drops the table; for tests */

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
/* The item with its contents rebuilt, in no room and on nobody; nullptr (and
 * nothing left behind) if any prototype is gone or the nesting is impossible. */
struct obj_data* bank_obj_from_records(const std::vector<objects_json::ObjectRecord>& records);
bool bank_obj_storable(struct obj_data* obj); /* false if it, or anything inside it, can't be rented */
const char* bank_slot_name(const bank_slot& slot); /* short description of the stored item */

struct bank_balance_row {
    std::string name;
    int inside; /* objects inside a container; < 0 when it holds nothing */
    std::string fee; /* already worded; unused when the fee column is off */
};
std::string format_bank_balance(const std::string& coins, int coin_limit_gold, int slots_used, int slots_max,
    const std::vector<bank_balance_row>& rows, bool show_fee);

#endif
