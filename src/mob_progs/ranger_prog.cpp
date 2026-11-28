#include "ranger_prog.h"

#include "../comm.h"
#include "../db.h"
#include "../interpre.h"
#include "../mob_options.h"
#include "../structs.h"
#include "../utils.h"

#include <cstdio>
#include <unordered_map>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;

SPECIAL(mob_ranger_new);

namespace {

std::unordered_map<int, ranger_config> g_ranger_configs; /* by mob rnum */

bool is_ranger_proto(int rnum) { return is_ranger_candidate(&mob_proto[rnum], rnum); }

} // namespace

ranger_config parse_ranger_options(const char* text, std::vector<vendor_problem>* problems)
{
    ranger_config config;
    const mob_yes_no_setting settings[] = {
        { "hide", &config.hide },
        { "ambush", &config.ambush },
        { "track", &config.track },
        { "riposte", &config.riposte },
    };
    parse_yes_no_options(text, settings, sizeof(settings) / sizeof(settings[0]), problems);
    return config;
}

bool is_ranger_candidate(const struct char_data* proto, int rnum)
{
    return mob_program_candidate(proto, rnum, (special_func)mob_ranger_new, PROG_RANGER);
}

void ranger_config_check(const struct char_data* proto, int mob_vnum, const struct char_data* builder)
{
    std::vector<vendor_problem> problems;
    parse_ranger_options(proto->specials.mob_options, &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_vnum, problem), builder);
}

std::vector<std::string> ranger_flag_warnings(const struct char_data* proto, int mob_vnum)
{
    static const std::pair<long, const char*> unsupported[] = { { MOB_SCAVENGER, "scavenger" },
        { MOB_BODYGUARD, "bodyguard" }, { MOB_ASSISTANT, "assistant" } };
    std::vector<std::string> lines;
    char buf[128];
    for (const auto& flag : unsupported) {
        if (!IS_SET(proto->specials2.act, flag.first))
            continue;
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: %s has no effect with program %d", mob_vnum,
            flag.second, PROG_RANGER);
        lines.push_back(buf);
    }
    return lines;
}

/* Soft checks, on /imp only, to the builder alone (not at boot, not logged). */
void ranger_implement_check(int mob_rnum, const struct char_data* builder)
{
    if (!builder || mob_rnum < 0 || mob_rnum > top_of_mobt || !is_ranger_proto(mob_rnum))
        return;
    for (const std::string& line : ranger_flag_warnings(&mob_proto[mob_rnum], mob_index[mob_rnum].virt)) {
        send_to_char(line.c_str(), builder);
        send_to_char("\n\r", builder);
    }
}

void ranger_config_rebuild(int mob_rnum, const struct char_data* builder, bool report)
{
    if (mob_rnum < 0 || mob_rnum > top_of_mobt) {
        g_ranger_configs.erase(mob_rnum);
        return;
    }
    if (!is_ranger_proto(mob_rnum)) {
        /* When /implement leaves the type with no program (/29 0, or the spec
         * flag cleared), copies already loaded keep running this one until they
         * reset, and read the type's settings still. */
        if (mob_proto[mob_rnum].specials.mob_options)
            g_ranger_configs[mob_rnum] = parse_ranger_options(mob_proto[mob_rnum].specials.mob_options, nullptr);
        else
            g_ranger_configs.erase(mob_rnum);
        return;
    }
    std::vector<vendor_problem> problems;
    g_ranger_configs[mob_rnum] = parse_ranger_options(mob_proto[mob_rnum].specials.mob_options, &problems);
    if (!report)
        return;
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_index[mob_rnum].virt, problem), builder);
}

const ranger_config* ranger_config_for(int mob_rnum)
{
    auto it = g_ranger_configs.find(mob_rnum);
    return it == g_ranger_configs.end() ? nullptr : &it->second;
}

const ranger_config& ranger_settings(const struct char_data* mob)
{
    static const ranger_config none;
    if (!mob_runs_program(mob, (special_func)mob_ranger_new, PROG_RANGER))
        return none;
    const ranger_config* config = ranger_config_for(mob->nr);
    return config ? *config : none;
}

void ranger_config_boot()
{
    g_ranger_configs.clear();
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum)
        if (is_ranger_proto(rnum))
            ranger_config_rebuild(rnum, nullptr);
}
