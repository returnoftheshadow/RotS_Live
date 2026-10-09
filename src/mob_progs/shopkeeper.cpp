#include "shopkeeper.h"

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <unordered_map>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct room_data world;
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern struct time_info_data time_info;
extern int no_specials;

ACMD(do_say);
int get_number(char** name);

using mob_options_detail::split_lines;
using mob_options_detail::trim;

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

bool vendor_hours_open(const std::vector<vendor_hours_window>& hours, int hour)
{
    if (hours.empty())
        return true;
    for (const vendor_hours_window& w : hours) {
        bool open = w.open < w.close ? (hour >= w.open && hour < w.close) : (hour >= w.open || hour < w.close);
        if (open)
            return true;
    }
    return false;
}

bool vendor_is_open(const vendor_config& config, int hour)
{
    return vendor_hours_open(config.hours, hour);
}

vendor_config parse_vendor_options(
    const char* text, const vendor_lookups& lookups, std::vector<vendor_problem>* problems)
{
    vendor_config config;
    bool saw_store = false, saw_hours = false, saw_list = false, saw_attacks = false;
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
        if (words.empty()) /* only \v or \f: trim keeps them, split_words drops them */
            continue;

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
        } else if (key == "list" && eq != std::string::npos) {
            if (saw_list) {
                problem(line_no, "duplicate list - line ignored");
                continue;
            }
            saw_list = true;
            config.list_message = value;
        } else if (key == "attacks" && eq != std::string::npos) {
            if (saw_attacks) {
                problem(line_no, "duplicate attacks - line ignored");
                continue;
            }
            saw_attacks = true;
            if (value == "yes" || value == "no")
                config.attacks = value == "yes";
            else
                problem(line_no, "bad attacks - line ignored");
        } else if (mob_option_is_general(key)) {
            continue; /* another feature's line (mob_general_options) */
        } else {
            problem(line_no, "unknown setting - line ignored");
        }
    }
    if (!saw_store)
        problem(0, "store missing - vendor disabled");
    return config;
}

namespace {

std::vector<std::string> wrap_words(const std::string& text, size_t width)
{
    std::vector<std::string> chunks;
    std::string current;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t space = text.find(' ', pos);
        std::string word = text.substr(pos, space == std::string::npos ? std::string::npos : space - pos);
        pos = space == std::string::npos ? text.size() : space + 1;
        while (word.size() > width) { /* a single over-long word is cut */
            if (!current.empty()) {
                chunks.push_back(current);
                current.clear();
            }
            chunks.push_back(word.substr(0, width));
            word = word.substr(width);
        }
        if (current.empty())
            current = word;
        else if (current.size() + 1 + word.size() <= width)
            current += " " + word;
        else {
            chunks.push_back(current);
            current = word;
        }
    }
    if (!current.empty() || chunks.empty())
        chunks.push_back(current);
    return chunks;
}

std::unordered_map<int, vendor_config> g_vendor_configs; /* by mob rnum */
/* Unusable vendors already logged when someone tried to trade; a rebuild
 * (boot, /save, /imp) reports the options again and clears the entry. */
std::set<int> g_vendor_unusable_logged; /* by mob rnum */

vendor_lookups game_lookups()
{
    vendor_lookups lookups;
    lookups.obj_exists = [](int vnum) { return real_object(vnum) >= 0; };
    lookups.room_exists = [](int vnum) { return real_room(vnum) >= 0; };
    return lookups;
}

} // namespace

void vendor_send(const std::string& line, struct char_data* builder)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line.c_str());
    mudlog(buf, NRM, LEVEL_AREAGOD, TRUE);
    if (builder && !mudlog_reaches(builder, LEVEL_AREAGOD, NRM)) {
        send_to_char(buf, builder);
        send_to_char("\n\r", builder);
    }
}

namespace {

bool is_vendor_proto(int rnum)
{
    return is_vendor_candidate(&mob_proto[rnum], rnum);
}

/* do_say refuses mobs with INT < 6 ("too stupid to talk"), which would leave
 * a vendor unable to say why it won't serve. Warn the builder rather than
 * special-case say. */
void vendor_add_speech_problem(const char_data& proto, std::vector<vendor_problem>* problems)
{
    if (proto.abilities.intel < 6)
        problems->push_back({ 0, "intelligence below 6 - vendor can't speak" });
}

} // namespace

/* True only when proto is a program-33 candidate AND nothing hard-coded
 * (ASSIGNMOB/.shp) already owns mob rnum's function slot. For a hard-coded
 * mob, store_prog_number isn't a program number at all: it's data the
 * hard-coded procedure reads directly (guild's guildmasters[] index,
 * ferry_captain's route, etc. -- see spec_pro.cpp), and coincidentally
 * overlapping 33 doesn't make the mob a vendor. rnum < 0 (a mob not in the
 * table yet) has no function slot to own. */
bool is_vendor_candidate(const struct char_data* proto, int rnum)
{
    /* -s boots leave every hard-coded func unassigned, so a guildmaster whose
     * guild number is 33 would look like a vendor. Vendors are specials too. */
    if (no_specials)
        return false;
    if (!IS_SET(proto->specials2.act, MOB_SPEC) || proto->specials.store_prog_number != PROG_BARTER_VENDOR)
        return false;
    return rnum < 0 || !mob_index[rnum].func || mob_index[rnum].func == (special_func)barter_vendor;
}

std::string format_vendor_list(const std::vector<vendor_list_row>& rows)
{
    std::vector<std::string> names;
    size_t width = 0;
    for (const vendor_list_row& row : rows) {
        std::string name = row.name;
        if (row.left >= 0)
            name += " (" + std::to_string(row.left) + " left)";
        width = std::max(width, std::min(name.size(), VENDOR_LIST_NAME_COLUMN_MAX));
        names.push_back(name);
    }
    std::string out;
    const size_t cost_width = 78 - (4 + width + 2); /* what is left of the line for a cost */
    for (size_t i = 0; i < rows.size(); ++i) {
        std::vector<std::string> chunks = wrap_words(names[i], VENDOR_LIST_NAME_COLUMN_MAX);
        std::vector<std::string> costs; /* one cost per line; a long currency name wraps */
        for (const vendor_list_cost& cost : rows[i].costs)
            for (const std::string& part : wrap_words(std::to_string(cost.qty) + " x " + cost.name, cost_width))
                costs.push_back(part);
        size_t lines = std::max(chunks.size(), costs.size());
        for (size_t l = 0; l < lines; ++l) {
            char number[8];
            snprintf(number, sizeof(number), "%2d. ", (int)(i + 1));
            std::string line = l == 0 ? number : "    ";
            std::string chunk = l < chunks.size() ? chunks[l] : "";
            line += chunk;
            line.append(width - chunk.size() + 2, ' ');
            if (l < costs.size())
                line += costs[l];
            line.erase(line.find_last_not_of(' ') + 1);
            out += line + "\n\r";
        }
    }
    return out;
}

std::vector<vendor_shortfall> vendor_shortfalls(const std::vector<vendor_cost>& costs,
    const std::function<int(int)>& have_count)
{
    std::vector<vendor_shortfall> short_of;
    for (const vendor_cost& cost : costs) {
        int have = have_count(cost.obj_vnum);
        if (have < cost.qty)
            short_of.push_back({ cost.obj_vnum, cost.qty, have });
    }
    return short_of;
}

std::string vendor_problem_line(int mob_vnum, const vendor_problem& problem)
{
    char buf[512];
    if (problem.line > 0)
        snprintf(buf, sizeof(buf), "MOB ERROR: mobile #%d, options line %d: %s", mob_vnum, problem.line, problem.text.c_str());
    else
        snprintf(buf, sizeof(buf), "MOB ERROR: mobile #%d: %s", mob_vnum, problem.text.c_str());
    return buf;
}

std::string vendor_wrap(const std::string& text, const std::string& indent)
{
    std::string out, rest = text;
    while (rest.size() > 78) {
        size_t lead = rest.find_first_not_of(' '); /* never break inside the leading indent */
        size_t cut = rest.rfind(' ', 78);
        if (cut == std::string::npos || lead == std::string::npos || cut <= lead)
            cut = 78;
        out += rest.substr(0, cut) + "\n\r";
        rest.erase(0, rest[cut] == ' ' ? cut + 1 : cut);
        rest.insert(0, indent);
    }
    return out + rest + "\n\r";
}

int vendor_takes_no_turn(bool attacks) { return attacks ? FALSE : TRUE; }

SPECIAL(shop_keeper); /* shop.cpp */
SPECIAL(banker); /* banker.cpp */
void* virt_program_number(int number);

bool mob_is_keeper(const struct char_data* mob)
{
    if (!mob || !IS_NPC(mob) || mob->nr < 0 || !mob_index || mob->nr > top_of_mobt)
        return false;
    /* The program the game would call, in the order activate_char_special
     * (interpre.cpp) tries them: the index function only with MOB_SPEC and
     * specials on, else the mob's own program number. */
    void* program = nullptr;
    if (mob_index[mob->nr].func && IS_SET(mob->specials2.act, MOB_SPEC) && !no_specials)
        program = (void*)mob_index[mob->nr].func;
    else if (mob->specials.store_prog_number)
        program = virt_program_number(mob->specials.store_prog_number);
    return program && (program == (void*)shop_keeper || program == (void*)barter_vendor || program == (void*)banker);
}

bool keeper_protection(struct char_data* host, struct char_data* ch, int cmd, int callflag,
    struct waiting_type* wtl, bool attacks, int* answer)
{
    switch (callflag) {
    case SPECIAL_SELF:
        *answer = vendor_takes_no_turn(attacks);
        return true;
    case SPECIAL_DAMAGE:
        if (ch && ch != host) /* a poison tick is cancelled without the keeper talking */
            vendor_tell(host, ch, "Don't even think about it.");
        *answer = TRUE;
        return true;
    case SPECIAL_TARGET:
        *answer = FALSE;
        if (ch && ch != host && cmd == CMD_BLINDING && wtl && wtl->targ1.type == TARGET_CHAR
            && wtl->targ1.ptr.ch == host) {
            vendor_tell(host, ch, "Don't even think about it.");
            *answer = TRUE;
        }
        return true;
    }
    return false;
}

void vendor_fight_flag_warnings(int mob_rnum, struct char_data* builder)
{
    if (!builder || mob_rnum < 0 || mob_rnum > top_of_mobt)
        return;
    const std::pair<long, const char*> flags[] = { { MOB_SCAVENGER, "SCAVENGER" }, { MOB_AGGRESSIVE, "AGGR" },
        { MOB_MEMORY, "MEMORY" }, { MOB_HELPER, "HELPER" }, { MOB_BODYGUARD, "BODYGUARD" }, { MOB_HUNTER, "HUNTER" },
        { MOB_ASSISTANT, "ASSISTANT" } };
    char buf[128];
    for (const auto& flag : flags) {
        if (!IS_SET(mob_proto[mob_rnum].specials2.act, flag.first))
            continue;
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: %s flag with attacks=no\n\r", mob_index[mob_rnum].virt,
            flag.second);
        send_to_char(buf, builder);
    }
}

void vendor_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder)
{
    std::vector<vendor_problem> problems;
    parse_vendor_options(proto->specials.mob_options, game_lookups(), &problems);
    vendor_add_speech_problem(*proto, &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_vnum, problem), builder);
}

/* Soft checks, reminded on /imp only, to the builder alone: not at boot and
 * not in the imm log. Scripted mobs and shopkeepers carry NOBASH by
 * convention; a list= header past 78 columns still shows, but wraps. */
void vendor_implement_check(int mob_rnum, struct char_data* builder)
{
    if (!builder || mob_rnum < 0 || mob_rnum > top_of_mobt || !is_vendor_proto(mob_rnum))
        return;
    char buf[128];
    if (!IS_SET(mob_proto[mob_rnum].specials2.act, MOB_NOBASH)) {
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: nobash not set\n\r", mob_index[mob_rnum].virt);
        send_to_char(buf, builder);
    }
    vendor_config config = parse_vendor_options(mob_proto[mob_rnum].specials.mob_options, game_lookups(), nullptr);
    if (!config.attacks)
        vendor_fight_flag_warnings(mob_rnum, builder);
    if (config.list_message.size() > VENDOR_LIST_MESSAGE_MAX) {
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: list longer than %d columns\n\r",
            mob_index[mob_rnum].virt, (int)VENDOR_LIST_MESSAGE_MAX);
        send_to_char(buf, builder);
    }
}

void vendor_config_rebuild(int mob_rnum, struct char_data* builder, bool report)
{
    g_vendor_unusable_logged.erase(mob_rnum);
    if (mob_rnum < 0 || mob_rnum > top_of_mobt || !is_vendor_proto(mob_rnum)) {
        g_vendor_configs.erase(mob_rnum);
        return;
    }
    std::vector<vendor_problem> problems;
    g_vendor_configs[mob_rnum] = parse_vendor_options(mob_proto[mob_rnum].specials.mob_options, game_lookups(), &problems);
    if (!report)
        return;
    vendor_add_speech_problem(mob_proto[mob_rnum], &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_index[mob_rnum].virt, problem), builder);
}

const vendor_config* vendor_config_for(int mob_rnum)
{
    auto it = g_vendor_configs.find(mob_rnum);
    return it == g_vendor_configs.end() ? nullptr : &it->second;
}

void vendor_config_boot()
{
    g_vendor_configs.clear();
    g_vendor_unusable_logged.clear();
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum) {
        if (!is_vendor_proto(rnum))
            continue;
        vendor_config_rebuild(rnum, nullptr);
    }
}

/* Vendors and bankers speak the way the old shopkeepers (shop.cpp) do: a
 * refusal to serve at all is a real `say` the room hears, every other reply
 * is a `tell` to the customer. */
void vendor_say(struct char_data* vendor, const char* text)
{
    char buf[MAX_INPUT_LENGTH];
    snprintf(buf, sizeof(buf), "%s", text);
    do_say(vendor, buf, 0, 0, 0);
}

void vendor_tell(struct char_data* vendor, struct char_data* ch, const char* text)
{
    if (!ch)
        return;
    /* The line do_tell sends, sent directly: a keeper's answer is the only
     * reply the command gets, so it must also reach a customer who has tells
     * turned off or whom the keeper can't see, where do_tell stays silent.
     * act() reads '$' as a code, so it is doubled, as typed input is; the
     * length cut must not leave half a pair. */
    std::string line;
    for (const char* c = text; *c; ++c)
        line.append(*c == '$' ? 2 : 1, *c);
    if (line.size() > VENDOR_TELL_MAX) {
        line.resize(VENDOR_TELL_MAX);
        size_t dollars = line.size() - (line.find_last_not_of('$') + 1);
        if (dollars % 2)
            line.pop_back();
    }
    char buf[VENDOR_TELL_MAX + 32];
    snprintf(buf, sizeof(buf), "$CT$n tells you '%s'", line.c_str());
    act(buf, FALSE, vendor, NULL, ch, TO_VICT);
}

/* command_interpreter parses the command's targets first and, after the
 * program returns, still runs the SPECIAL_TARGET specials on them. An object
 * the program destroys, or anything inside it, must not be left in them. */
void vendor_forget_target(struct waiting_type* wtl, struct obj_data* obj)
{
    if (!wtl)
        return;
    for (struct target_data* target : { &wtl->targ1, &wtl->targ2 }) {
        if (target->type != TARGET_OBJ)
            continue;
        for (struct obj_data* held = target->ptr.obj; held; held = held->in_obj)
            if (held == obj) {
                target->type = TARGET_NONE;
                target->ptr.other = 0;
                break;
            }
    }
}

bool vendor_serves_customer(struct char_data* vendor, struct char_data* ch)
{
    if (IS_AGGR_TO(vendor, ch)) {
        vendor_say(vendor, "Go away, I won't deal with you!");
        return false;
    }
    if (IS_SHADOW(ch)) {
        vendor_say(vendor, "Ugh! I'm not serving you!");
        return false;
    }
    if (!RP_RACE_CHECK(vendor, ch)) {
        vendor_say(vendor, "Sorry, I can't serve you!");
        return false;
    }
    if (!CAN_SEE(vendor, ch)) {
        vendor_say(vendor, "I don't trade with someone I can't see!");
        return false;
    }
    return true;
}

namespace {

bool vendor_serves(struct char_data* vendor, struct char_data* ch, const vendor_config& config)
{
    if (!vendor_serves_customer(vendor, ch))
        return false;
    if (!vendor_is_open(config, time_info.hours)) {
        vendor_say(vendor, "I'm closed. Come back later.");
        return false;
    }
    return true;
}

struct stock_row {
    const vendor_price* price;
    struct obj_data* copy; /* first visible copy in the store room */
    int count;
};

std::vector<stock_row> vendor_stock(struct char_data* ch, const vendor_config& config)
{
    std::vector<stock_row> rows;
    int room = real_room(config.store_vnum);
    if (room < 0)
        return rows;
    for (const vendor_price& price : config.prices) {
        int item_rnum = real_object(price.item_vnum);
        if (item_rnum < 0)
            continue;
        stock_row row { &price, 0, 0 };
        for (struct obj_data* obj = world[room].contents; obj; obj = obj->next_content)
            if (obj->item_number == item_rnum && CAN_SEE_OBJ(ch, obj)) {
                if (!row.copy)
                    row.copy = obj;
                ++row.count;
            }
        if (row.copy)
            rows.push_back(row);
    }
    return rows;
}

const char* obj_vnum_short(int vnum)
{
    int rnum = real_object(vnum);
    return rnum >= 0 ? obj_proto[rnum].short_description : "something";
}

/* Loose, EMPTY copies only: a container with things in it is never taken
 * as payment, so its contents can't be destroyed. */
std::vector<struct obj_data*> payable_copies(struct char_data* ch, int vnum)
{
    std::vector<struct obj_data*> copies;
    int rnum = real_object(vnum);
    for (struct obj_data* obj = ch->carrying; obj && rnum >= 0; obj = obj->next_content)
        if (obj->item_number == rnum && !obj->contains)
            copies.push_back(obj);
    return copies;
}

void vendor_list(struct char_data* vendor, struct char_data* ch, const vendor_config& config)
{
    std::vector<stock_row> stock = vendor_stock(ch, config);
    if (stock.empty()) {
        vendor_tell(vendor, ch, "I have nothing to sell right now.");
        return;
    }
    std::vector<vendor_list_row> rows;
    for (const stock_row& s : stock) {
        vendor_list_row row { s.copy->short_description, s.price->deduct ? s.count : -1, {} };
        for (const vendor_cost& cost : s.price->costs)
            row.costs.push_back({ cost.qty, obj_vnum_short(cost.obj_vnum) });
        rows.push_back(row);
    }
    std::string header = config.list_message.empty() ? VENDOR_DEFAULT_LIST_MESSAGE : config.list_message;
    send_to_char((header + "\n\r").c_str(), ch);
    send_to_char(format_vendor_list(rows).c_str(), ch);
}

void vendor_buy(struct char_data* vendor, struct char_data* ch, char* arg, const vendor_config& config,
    struct waiting_type* wtl)
{
    char want[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    one_argument(arg, want);
    if (!*want) {
        vendor_tell(vendor, ch, "What do you want to buy?");
        return;
    }
    std::vector<stock_row> stock = vendor_stock(ch, config);
    const stock_row* pick = 0;
    if (strspn(want, "0123456789") == strlen(want)) { /* a list number */
        int n = strlen(want) <= 4 ? atoi(want) : 0;
        if (n >= 1 && n <= (int)stock.size())
            pick = &stock[n - 1];
    } else { /* a keyword, "2.belt" meaning the second listed row it matches */
        char* name = want;
        int nth = get_number(&name);
        for (const stock_row& s : stock)
            if (isname(name, s.copy->name) && --nth == 0) {
                pick = &s;
                break;
            }
    }
    if (!pick) {
        vendor_tell(vendor, ch, "I don't have that. Try 'list'.");
        return;
    }
    std::vector<vendor_shortfall> short_of = vendor_shortfalls(pick->price->costs,
        [ch](int vnum) { return (int)payable_copies(ch, vnum).size(); });
    if (!short_of.empty()) {
        /* One tell, however many items are short. */
        std::string need = "You need ";
        for (size_t i = 0; i < short_of.size(); ++i) {
            const char* joiner = "";
            if (i > 0)
                joiner = i + 1 == short_of.size() ? " and " : ", ";
            snprintf(buf, sizeof(buf), "%s%d x %s (you have %d)", joiner, short_of[i].need,
                obj_vnum_short(short_of[i].obj_vnum), short_of[i].have);
            need += buf;
        }
        vendor_tell(vendor, ch, (need + ".").c_str());
        return;
    }

    /* Gather every payment object first; destroy nothing until all are in hand. */
    std::vector<struct obj_data*> payment;
    for (const vendor_cost& cost : pick->price->costs) {
        std::vector<struct obj_data*> copies = payable_copies(ch, cost.obj_vnum);
        if ((int)copies.size() < cost.qty) {
            snprintf(buf, sizeof(buf), "SYSERR: barter_vendor: payment count changed for obj #%d", cost.obj_vnum);
            mudlog(buf, NRM, LEVEL_IMMORT, TRUE);
            return;
        }
        payment.insert(payment.end(), copies.begin(), copies.begin() + cost.qty);
    }

    /* Room to carry it, counted as it will be after paying: the payment
     * leaves as the item arrives. deduct hands over the store copy itself
     * (with anything inside it); otherwise a fresh, empty copy. */
    int payment_weight = 0;
    for (struct obj_data* obj : payment)
        payment_weight += GET_OBJ_WEIGHT(obj);
    int item_weight = pick->price->deduct ? GET_OBJ_WEIGHT(pick->copy)
                                          : obj_proto[pick->copy->item_number].obj_flags.weight;
    if (IS_CARRYING_N(ch) - (int)payment.size() + 1 > CAN_CARRY_N(ch)) {
        send_to_char("You can't carry that many items.\n\r", ch);
        return;
    }
    if (IS_CARRYING_W(ch) - payment_weight + item_weight > CAN_CARRY_W(ch)) {
        send_to_char("You can't carry that much weight.\n\r", ch);
        return;
    }

    for (struct obj_data* obj : payment) {
        vendor_forget_target(wtl, obj); /* "buy ruby" also names the ruby paid with */
        obj_from_char(obj);
        extract_obj(obj);
    }

    std::string paid; /* one currency per line, wrapped, so it stays within 78 columns */
    for (const vendor_cost& cost : pick->price->costs)
        paid += vendor_wrap("  " + std::to_string(cost.qty) + " x " + obj_vnum_short(cost.obj_vnum), "    ");
    struct obj_data* bought;
    if (pick->price->deduct) {
        bought = pick->copy;
        obj_from_room(bought);
    } else
        bought = read_object(pick->copy->item_number, REAL);
    obj_to_char(bought, ch);
    bought->touched = 1; /* a player has it now: later gets are logged */
    send_to_char(("You hand over:\n\r" + paid + vendor_wrap(std::string("You now have ") + bought->short_description + ".")).c_str(), ch);
    act("$n buys $p.", FALSE, ch, bought, 0, TO_ROOM);

    std::string trade = std::string("VENDOR: ") + GET_NAME(ch) + " buys " + bought->short_description + " ("
        + std::to_string(obj_index[bought->item_number].virt) + ") from mobile #"
        + std::to_string(vendor->nr >= 0 ? mob_index[vendor->nr].virt : -1) + ", paid";
    for (size_t i = 0; i < pick->price->costs.size(); ++i) {
        const vendor_cost& cost = pick->price->costs[i];
        trade += (i ? ", " : " ") + std::to_string(cost.qty) + " x " + obj_vnum_short(cost.obj_vnum) + " ("
            + std::to_string(cost.obj_vnum) + ")";
    }
    log(trade.c_str());
}

} // namespace

/* True exactly when do_give (act_obj1.cpp) would pick this vendor as the
 * recipient. It parses the same way: half_chop off the first word; after a
 * number ("give 10 coins trader") the target comes from the rest, otherwise
 * from the whole argument; either way argument_interpreter takes the object
 * word, then the target word (skipping fill words like "to"), and
 * give_find_vict looks the target up with get_char_room_vis. */
bool give_targets(struct char_data* vendor, struct char_data* ch, char* arg)
{
    char whole[MAX_INPUT_LENGTH], first[MAX_INPUT_LENGTH], rest[MAX_INPUT_LENGTH];
    char what[MAX_INPUT_LENGTH], target[MAX_INPUT_LENGTH];

    strncpy(whole, arg, sizeof(whole) - 1);
    whole[sizeof(whole) - 1] = 0;
    half_chop(whole, first, rest);
    if (!*first)
        return false;
    argument_interpreter(is_number(first) ? rest : whole, what, target);
    if (!*what || !*target)
        return false;
    return get_char_room_vis(ch, target) == vendor;
}

SPECIAL(barter_vendor)
{
    /* Only a registered vendor mob is ever a vendor. The old program path can
     * call program 33 for a player or an unregistered mob; neither may become
     * invulnerable or intercept commands. */
    if (!host || !IS_NPC(host))
        return FALSE;
    const vendor_config* config = vendor_config_for(host->nr);
    if (!config)
        return FALSE;
    int answer;
    if (keeper_protection(host, ch, cmd, callflag, wtl, config->attacks, &answer)) /* shopkeeper.h */
        return answer;
    if (!ch || ch == host)
        return FALSE;
    if (callflag != SPECIAL_COMMAND)
        return FALSE;
    if (cmd != CMD_LIST && cmd != CMD_BUY && cmd != CMD_GIVE)
        return FALSE;

    if (cmd == CMD_GIVE) {
        if (!arg || !give_targets(host, ch, arg))
            return FALSE;
        vendor_tell(host, ch, "I don't take gifts.");
        return TRUE;
    }

    if (!config->usable()) {
        if (g_vendor_unusable_logged.insert(host->nr).second) /* once, not per command */
            vendor_send(vendor_problem_line(host->nr >= 0 ? mob_index[host->nr].virt : -1,
                            { 0, "bad options - vendor disabled" }),
                nullptr);
        vendor_say(host, "I'm not trading right now.");
        return TRUE;
    }
    if (!vendor_serves(host, ch, *config))
        return TRUE;

    if (cmd == CMD_LIST)
        vendor_list(host, ch, *config);
    else
        vendor_buy(host, ch, arg ? arg : (char*)"", *config, wtl);
    return TRUE;
}
