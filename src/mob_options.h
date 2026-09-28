#ifndef MOB_OPTIONS_H
#define MOB_OPTIONS_H

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

/* Mob options: a persisted multi-line text field for mob programs' settings
 * (one setting per line: "key", "key=value", or a keyword line like "price ...").
 * A line starting with // is a comment and is ignored. */

constexpr int MOB_OPTIONS_MAX = 4000;

/* True if a line is exactly `key` or `key=value` (spaces around '=' allowed);
 * *value receives the trimmed text after '=' (empty for a bare key). */
bool mob_option_find(const char* options, const char* key, std::string* value);

/* False, with a reason in *why, if saving `text` would corrupt a mob file:
 * over MOB_OPTIONS_MAX, any '#' or '~', or a leading '$'. */
bool mob_options_storable(const char* text, const char** why);

/* Removes leading blank lines and whitespace in place, as reading the text
 * back from the mob file would (read_mob_options skips them). */
void mob_options_trim_leading(char* text);

/* Reads the optional options string that may follow a mob record. Returns
 * nullptr, leaving the stream at the next token, if that token starts the
 * next record ('#' or '$') or the file ends, or if the text is empty. */
char* read_mob_options(FILE* f, char* context);

/* Writes the options string; writes nothing for null/empty text. */
void write_mob_options(FILE* f, const char* options);

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

#endif
