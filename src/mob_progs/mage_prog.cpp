#include "mage_prog.h"

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

SPECIAL(mob_magic_user_spec);

namespace {

std::unordered_map<int, mage_config> g_mage_configs; /* by mob rnum */

bool is_mage_proto(int rnum) { return is_mage_candidate(&mob_proto[rnum], rnum); }

} // namespace

mage_config parse_mage_options(const char* text, std::vector<vendor_problem>* problems)
{
    mage_config config;
    mob_yes_no_setting settings[mage_type_count + 3];
    for (int i = 0; i < mage_type_count; ++i)
        settings[i] = { mage_types_table[i].key, &(config.*mage_types_table[i].flag) };
    settings[mage_type_count] = { "conjurer", &config.conjurer };
    settings[mage_type_count + 1] = { "shield", &config.shield };
    settings[mage_type_count + 2] = { "terror", &config.terror };
    parse_yes_no_options(text, settings, mage_type_count + 3, problems);
    return config;
}

bool is_mage_candidate(const struct char_data* proto, int rnum)
{
    return mob_program_candidate(proto, rnum, (special_func)mob_magic_user_spec, PROG_MAGE);
}

void mage_config_check(const struct char_data* proto, int mob_vnum, const struct char_data* builder)
{
    std::vector<vendor_problem> problems;
    parse_mage_options(proto->specials.mob_options, &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_vnum, problem), builder);
}

std::vector<std::string> mage_setting_warnings(const char* options, int mob_vnum)
{
    std::vector<std::string> lines;
    if (parse_mage_options(options, nullptr).has_type())
        return lines;
    char buf[128];
    snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: no mage type set, no type spells", mob_vnum);
    lines.push_back(buf);
    return lines;
}

/* Soft checks, on /imp only, to the builder alone (not at boot, not logged). */
void mage_implement_check(int mob_rnum, const struct char_data* builder)
{
    if (!builder || mob_rnum < 0 || mob_rnum > top_of_mobt || !is_mage_proto(mob_rnum))
        return;
    for (const std::string& line :
        mage_setting_warnings(mob_proto[mob_rnum].specials.mob_options, mob_index[mob_rnum].virt)) {
        send_to_char(line.c_str(), builder);
        send_to_char("\n\r", builder);
    }
}

void mage_config_rebuild(int mob_rnum, const struct char_data* builder, bool report)
{
    if (mob_rnum < 0 || mob_rnum > top_of_mobt) {
        g_mage_configs.erase(mob_rnum);
        return;
    }
    if (!is_mage_proto(mob_rnum)) {
        /* When /implement leaves the type with no program (/29 0, or the spec
         * flag cleared), copies already loaded keep running this one until they
         * reset, and read the type's settings still. */
        if (mob_proto[mob_rnum].specials.mob_options)
            g_mage_configs[mob_rnum] = parse_mage_options(mob_proto[mob_rnum].specials.mob_options, nullptr);
        else
            g_mage_configs.erase(mob_rnum);
        return;
    }
    std::vector<vendor_problem> problems;
    g_mage_configs[mob_rnum] = parse_mage_options(mob_proto[mob_rnum].specials.mob_options, &problems);
    if (!report)
        return;
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_index[mob_rnum].virt, problem), builder);
}

const mage_config* mage_config_for(int mob_rnum)
{
    auto it = g_mage_configs.find(mob_rnum);
    return it == g_mage_configs.end() ? nullptr : &it->second;
}

const mage_config& mage_settings(const struct char_data* mob)
{
    static const mage_config none;
    if (!mob_runs_program(mob, (special_func)mob_magic_user_spec, PROG_MAGE))
        return none;
    const mage_config* config = mage_config_for(mob->nr);
    return config ? *config : none;
}

void mage_config_boot()
{
    g_mage_configs.clear();
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum)
        if (is_mage_proto(rnum))
            mage_config_rebuild(rnum, nullptr);
}
