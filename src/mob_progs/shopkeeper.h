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
