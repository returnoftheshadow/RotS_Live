#include "passive.h"

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern int no_specials;

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

vendor_lookups game_lookups()
{
    vendor_lookups lookups;
    lookups.obj_exists = [](int vnum) { return real_object(vnum) >= 0; };
    lookups.room_exists = [](int vnum) { return real_room(vnum) >= 0; };
    return lookups;
}

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

bool is_vendor_proto(const char_data& proto)
{
    return IS_SET(proto.specials2.act, MOB_SPEC) && proto.specials.store_prog_number == PROG_BARTER_VENDOR;
}

/* do_say refuses mobs with INT < 6 ("too stupid to talk"), which would leave
 * a vendor unable to answer. Warn the builder rather than special-case say. */
void vendor_add_speech_problem(const char_data& proto, std::vector<vendor_problem>* problems)
{
    if (proto.abilities.intel < 6)
        problems->push_back({ 0, "intelligence below 6 - vendor can't speak" });
}

} // namespace

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
    for (size_t i = 0; i < rows.size(); ++i) {
        std::vector<std::string> chunks = wrap_words(names[i], VENDOR_LIST_NAME_COLUMN_MAX);
        size_t lines = std::max(chunks.size(), rows[i].costs.size());
        for (size_t l = 0; l < lines; ++l) {
            char number[8];
            snprintf(number, sizeof(number), "%2d. ", (int)(i + 1));
            std::string line = l == 0 ? number : "    ";
            std::string chunk = l < chunks.size() ? chunks[l] : "";
            line += chunk;
            line.append(width - chunk.size() + 2, ' ');
            if (l < rows[i].costs.size())
                line += std::to_string(rows[i].costs[l].qty) + " x " + rows[i].costs[l].name;
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

void vendor_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder)
{
    std::vector<vendor_problem> problems;
    parse_vendor_options(proto->specials.mob_options, game_lookups(), &problems);
    vendor_add_speech_problem(*proto, &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_vnum, problem), builder);
}

void vendor_config_rebuild(int mob_rnum, struct char_data* builder)
{
    if (mob_rnum < 0 || mob_rnum > top_of_mobt || !is_vendor_proto(mob_proto[mob_rnum])) {
        g_vendor_configs.erase(mob_rnum);
        return;
    }
    std::vector<vendor_problem> problems;
    g_vendor_configs[mob_rnum] = parse_vendor_options(mob_proto[mob_rnum].specials.mob_options, game_lookups(), &problems);
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
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum) {
        if (!is_vendor_proto(mob_proto[rnum]))
            continue;
        vendor_config_rebuild(rnum, nullptr);
        if (!no_specials && mob_index[rnum].func && mob_index[rnum].func != (special_func)barter_vendor)
            vendor_send(vendor_problem_line(mob_index[rnum].virt,
                            { 0, "program 33 overridden by hard-coded procedure" }),
                nullptr);
    }
}

SPECIAL(barter_vendor)
{
    return FALSE; /* Task 6 fills this in */
}
