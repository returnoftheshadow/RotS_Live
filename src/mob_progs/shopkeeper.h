#ifndef MOB_PROGS_SHOPKEEPER_H
#define MOB_PROGS_SHOPKEEPER_H

/* Barter vendor / shopkeeper mob program (program 33): reacts to what
 * players buy and sell, doesn't fight or roam. */

#include "../mob_options.h"

#include <functional>
#include <string>
#include <vector>

/* Barter vendor settings: "store=<room vnum>", "hours=<windows>", and
 * "price <item vnum> <obj vnum>x<qty> ... [deduct]" lines. */

constexpr int VENDOR_MAX_PRICE_LINES = 30;
constexpr int VENDOR_MAX_CURRENCIES = 4;
constexpr int VENDOR_MAX_QTY = 100;

struct vendor_cost {
    int obj_vnum;
    int qty;
};
struct vendor_price {
    int item_vnum;
    std::vector<vendor_cost> costs;
    bool deduct;
    int line;
};
struct vendor_hours_window {
    int open;
    int close;
}; /* open at `open`, closed from `close` */
struct vendor_config {
    int store_vnum = -1;
    bool store_ok = false;
    bool hours_ok = true;
    std::vector<vendor_hours_window> hours; /* empty = always open */
    std::vector<vendor_price> prices; /* in options order */
    std::string list_message; /* empty = the default list header */
    bool attacks = true; /* false: the mob AI never runs, so the vendor never starts a fight */
    bool usable() const { return store_ok && hours_ok; }
};
struct vendor_problem {
    int line;
    std::string text;
}; /* line 0 = whole config */
struct vendor_lookups {
    std::function<bool(int)> obj_exists;
    std::function<bool(int)> room_exists;
};

/* Parses the mob options text into a vendor_config, appending a vendor_problem
 * for every rejected/skipped/ignored line (or line 0 for whole-config issues)
 * to *problems when non-null. */
vendor_config parse_vendor_options(
    const char* text, const vendor_lookups& lookups, std::vector<vendor_problem>* problems);

/* Parses an "hours=" value like "6-12,14-20" into windows; false on any bad
 * window (out of range, equal open/close, trailing comma, etc). */
bool vendor_hours_parse(const std::string& value, std::vector<vendor_hours_window>* out);

/* True if `hour` (0-23) falls in any of config's open windows, or config has
 * no hours at all (always open). */
bool vendor_is_open(const vendor_config& config, int hour);
bool vendor_hours_open(const std::vector<vendor_hours_window>& hours, int hour); /* empty = always open */

constexpr int PROG_BARTER_VENDOR = 33;
constexpr size_t VENDOR_LIST_NAME_COLUMN_MAX = 38;
constexpr const char* VENDOR_DEFAULT_LIST_MESSAGE = "What would you like to trade?";
constexpr size_t VENDOR_LIST_MESSAGE_MAX = 78;

struct char_data;
struct waiting_type;
int barter_vendor(struct char_data* host, struct char_data* ch, int cmd, char* arg, int callflag, struct waiting_type* wtl);
void vendor_config_boot(); /* all program-33 prototypes */
/* parse + store (and report problems unless !report), or erase */
void vendor_config_rebuild(int mob_rnum, struct char_data* builder, bool report = true);
/* program 33 with MOB_SPEC, and no hard-coded function owns rnum (rnum < 0: none yet) */
bool is_vendor_candidate(const struct char_data* proto, int rnum);
void vendor_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder); /* report only */
void vendor_implement_check(int mob_rnum, struct char_data* builder); /* /imp only: nobash reminder */
const vendor_config* vendor_config_for(int mob_rnum); /* nullptr if none */
std::string vendor_problem_line(int mob_vnum, const vendor_problem& problem); /* formatted warning */

/* Shared with the other service programs (banker). */
/* As the old shopkeepers: a refusal to serve is a `say`, any other reply a `tell`. */
void vendor_say(struct char_data* vendor, const char* text);
void vendor_tell(struct char_data* vendor, struct char_data* ch, const char* text);
constexpr size_t VENDOR_TELL_MAX = 1000; /* longest text a tell carries; more is cut */
/* Breaks a line at spaces so no part passes 78 columns; adds the line end. */
std::string vendor_wrap(const std::string& text, const std::string& indent = ""); /* indent: lines after the first */
/* ---- Keeper protection. A keeper is a mob whose program serves players:
 * an old shopkeeper (shop.cpp), a barter vendor, a banker. The protection
 * lives here, in one place, so that no other code names the keeper types:
 *   - the rest of the game asks mob_is_keeper();
 *   - a keeper program calls keeper_protection() first.
 * A new keeper type gets all of it by adding its program to the list in
 * mob_is_keeper() (shopkeeper.cpp) and making that one call. */
bool mob_is_keeper(const struct char_data* mob);
/* True when the call was one the protection answers, with the program's
 * return value in *answer:
 *   SPECIAL_SELF   - the mob AI's turn: see vendor_takes_no_turn;
 *   SPECIAL_DAMAGE - an attack (or a poison tick, damage(host, host, ...)) is
 *                    cancelled; an attacker is told so;
 *   SPECIAL_TARGET - blinding dust is refused: it blinds even when its damage
 *                    is cancelled, and a blind keeper serves nobody. */
bool keeper_protection(struct char_data* host, struct char_data* ch, int cmd, int callflag,
    struct waiting_type* wtl, bool attacks, int* answer);
/* "attacks=no": the answer a keeper program gives the mob AI on its own turn
 * (SPECIAL_SELF). TRUE ends the turn there, so the keeper never attacks,
 * assists, hunts, wanders or picks things up; a mob that never starts a
 * fight is never left open to one (damage() only asks the program while the
 * victim is not already fighting the attacker). */
int vendor_takes_no_turn(bool attacks);
/* /imp only, for a keeper with attacks=no: mob flags that can do nothing
 * (fighting, helping a master, picking things up). */
void vendor_fight_flag_warnings(int mob_rnum, struct char_data* builder);
void vendor_forget_target(struct waiting_type* wtl, struct obj_data* obj); /* before destroying obj in a command */
bool vendor_serves_customer(struct char_data* vendor, struct char_data* ch); /* every refusal but hours */
bool give_targets(struct char_data* vendor, struct char_data* ch, char* arg); /* would do_give pick this mob? */
void vendor_send(const std::string& line, struct char_data* builder); /* warning to the builder log */

struct vendor_list_cost {
    int qty;
    std::string name;
};
struct vendor_list_row {
    std::string name;
    int left; /* copies in stock for a deduct item; < 0 otherwise */
    std::vector<vendor_list_cost> costs;
};
std::string format_vendor_list(const std::vector<vendor_list_row>& rows);

struct vendor_shortfall {
    int obj_vnum;
    int need;
    int have;
};
std::vector<vendor_shortfall> vendor_shortfalls(const std::vector<vendor_cost>& costs,
    const std::function<int(int)>& have_count);

#endif
