#include "mob_options.h"

#include "db.h"
#include "utils.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

namespace mob_options_detail {

std::string trim(const std::string& s)
{
    const char* space = " \t\r\n";
    size_t b = s.find_first_not_of(space);
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(space) - b + 1);
}

std::vector<std::string> split_lines(const char* text)
{
    std::vector<std::string> lines;
    if (!text)
        return lines;
    std::string current;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (*p != '\r') {
            current += *p;
        }
    }
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

} // namespace mob_options_detail

using mob_options_detail::split_lines;
using mob_options_detail::trim;

bool mob_option_find(const char* options, const char* key, std::string* value)
{
    for (const std::string& raw : split_lines(options)) {
        std::string line = trim(raw);
        if (line.compare(0, 2, "//") == 0) /* comment */
            continue;
        size_t eq = line.find('=');
        std::string name = trim(eq == std::string::npos ? line : line.substr(0, eq));
        if (name != key)
            continue;
        if (value)
            *value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        return true;
    }
    return false;
}

bool mob_options_storable(const char* text, const char** why)
{
    if (!text)
        return true;
    if (strlen(text) > (size_t)MOB_OPTIONS_MAX) {
        *why = "options are too long (max 4000 characters)";
        return false;
    }
    /* The shape editor's record scanner (find_mob/replace_proto) treats any
     * '#' in a mob file as a record header, so '#' is refused anywhere. */
    if (strchr(text, '~') || strchr(text, '#')) {
        *why = "options can't contain # or ~";
        return false;
    }
    const char* p = text;
    while (*p && isspace((unsigned char)*p))
        ++p;
    if (*p == '$') {
        *why = "options can't start with $";
        return false;
    }
    return true;
}

char* read_mob_options(FILE* f, char* context)
{
    int c;
    do {
        c = fgetc(f);
    } while (c != EOF && isspace(c));
    if (c == EOF)
        return nullptr;
    ungetc(c, f);
    if (c == '#' || c == '$')
        return nullptr;
    char* text = fread_string(f, context);
    if (text && !*text) {
        RELEASE(text);
        return nullptr;
    }
    return text;
}

void write_mob_options(FILE* f, const char* options)
{
    if (!options || !*options)
        return;
    fprintf(f, "%s~\n", options);
}

namespace {

bool parse_int(const std::string& s, int* out)
{
    if (s.empty() || s.size() > 9)
        return false;
    for (char c : s)
        if (!isdigit((unsigned char)c))
            return false;
    *out = atoi(s.c_str());
    return true;
}

std::vector<std::string> split_words(const std::string& s)
{
    std::istringstream in(s);
    std::vector<std::string> words;
    std::string w;
    while (in >> w)
        words.push_back(w);
    return words;
}

bool parse_cost(const std::string& token, vendor_cost* out)
{
    size_t x = token.find('x');
    if (x == std::string::npos)
        return false;
    return parse_int(token.substr(0, x), &out->obj_vnum) && parse_int(token.substr(x + 1), &out->qty);
}

} // namespace

bool vendor_hours_parse(const std::string& value, std::vector<vendor_hours_window>* out)
{
    out->clear();
    std::string v = trim(value);
    if (v.empty())
        return false;
    size_t start = 0;
    for (;;) {
        size_t comma = v.find(',', start);
        std::string part = trim(v.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        size_t dash = part.find('-');
        int open, close;
        if (dash == std::string::npos || !parse_int(trim(part.substr(0, dash)), &open)
            || !parse_int(trim(part.substr(dash + 1)), &close) || open > 23 || close > 23 || open == close)
            return false;
        out->push_back({ open, close });
        if (comma == std::string::npos)
            return true;
        start = comma + 1;
    }
}

bool vendor_is_open(const vendor_config& config, int hour)
{
    if (config.hours.empty())
        return true;
    for (const vendor_hours_window& w : config.hours) {
        bool open = w.open < w.close ? (hour >= w.open && hour < w.close) : (hour >= w.open || hour < w.close);
        if (open)
            return true;
    }
    return false;
}

vendor_config parse_vendor_options(
    const char* text, const vendor_lookups& lookups, std::vector<vendor_problem>* problems)
{
    vendor_config config;
    bool saw_store = false, saw_hours = false;
    std::set<int> priced_items;
    auto problem = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what });
    };

    int line_no = 0;
    for (const std::string& raw : split_lines(text)) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0) /* blank or comment */
            continue;
        std::vector<std::string> words = split_words(line);

        if (words[0] == "price") {
            vendor_price price { 0, {}, false, line_no };
            size_t end = words.size();
            if (end > 1 && words[end - 1] == "deduct") {
                price.deduct = true;
                --end;
            }
            bool ok = end >= 3 && parse_int(words[1], &price.item_vnum);
            for (size_t i = 2; ok && i < end; ++i) {
                vendor_cost cost;
                ok = parse_cost(words[i], &cost);
                if (ok)
                    price.costs.push_back(cost);
            }
            if (!ok) {
                problem(line_no, "price: bad format - line skipped");
                continue;
            }
            if ((int)price.costs.size() > VENDOR_MAX_CURRENCIES) {
                problem(line_no, "price: more than 4 currencies - line skipped");
                continue;
            }
            std::string bad;
            std::set<int> seen;
            for (const vendor_cost& cost : price.costs) {
                if (cost.qty < 1 || cost.qty > VENDOR_MAX_QTY) {
                    bad = "price: quantity " + std::to_string(cost.qty) + " out of range - line skipped";
                    break;
                }
                if (!seen.insert(cost.obj_vnum).second) {
                    bad = "price: currency vnum " + std::to_string(cost.obj_vnum) + " listed twice - line skipped";
                    break;
                }
            }
            if (bad.empty() && !lookups.obj_exists(price.item_vnum))
                bad = "price: object vnum " + std::to_string(price.item_vnum) + " not found - line skipped";
            for (size_t i = 0; bad.empty() && i < price.costs.size(); ++i)
                if (!lookups.obj_exists(price.costs[i].obj_vnum))
                    bad = "price: object vnum " + std::to_string(price.costs[i].obj_vnum) + " not found - line skipped";
            if (bad.empty() && priced_items.count(price.item_vnum))
                bad = "price: duplicate item vnum " + std::to_string(price.item_vnum) + " - line skipped";
            if (bad.empty() && (int)config.prices.size() >= VENDOR_MAX_PRICE_LINES)
                bad = "price: more than 30 lines - line skipped";
            if (!bad.empty()) {
                problem(line_no, bad);
                continue;
            }
            priced_items.insert(price.item_vnum);
            config.prices.push_back(price);
            continue;
        }

        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        if (key == "store" && eq != std::string::npos) {
            if (saw_store) {
                problem(line_no, "duplicate store - line ignored");
                continue;
            }
            saw_store = true;
            int vnum;
            if (!parse_int(value, &vnum)) {
                problem(line_no, "bad store - vendor disabled");
                continue;
            }
            config.store_vnum = vnum;
            if (!lookups.room_exists(vnum)) {
                problem(line_no, "store room vnum " + std::to_string(vnum) + " not found - vendor disabled");
                continue;
            }
            config.store_ok = true;
        } else if (key == "hours" && eq != std::string::npos) {
            if (saw_hours) {
                problem(line_no, "duplicate hours - line ignored");
                continue;
            }
            saw_hours = true;
            if (!vendor_hours_parse(value, &config.hours)) {
                config.hours.clear();
                config.hours_ok = false;
                problem(line_no, "bad hours - vendor disabled");
            }
        } else {
            problem(line_no, "unknown setting - line ignored");
        }
    }
    if (!saw_store)
        problem(0, "store missing - vendor disabled");
    return config;
}
