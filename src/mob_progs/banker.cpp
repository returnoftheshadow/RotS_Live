#include "banker.h"

#include "../structs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

using mob_options_detail::split_lines;
using mob_options_detail::trim;

namespace {

bool parse_number(const std::string& s, int* out)
{
    if (s.empty() || s.size() > 9)
        return false;
    for (char c : s)
        if (!isdigit((unsigned char)c))
            return false;
    *out = atoi(s.c_str());
    return true;
}

/* Days since 1970-01-01 for a calendar date (proleptic Gregorian). */
long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

/* The bank day a moment falls in: its local date, or the day before when
 * the local hour is still short of the start hour. */
long bank_day_index(time_t when, int start_hour)
{
    struct tm local { };
    localtime_r(&when, &local);
    long day = days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    if (local.tm_hour < start_hour)
        --day;
    return day;
}

} // namespace

banker_config parse_banker_options(const char* text, std::vector<vendor_problem>* problems)
{
    banker_config config;
    bool saw_hours = false, saw_fee = false, saw_maxdays = false, saw_markup = false;
    auto problem = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what });
    };
    auto strict = [&](int line, const char* key) {
        config.ok = false;
        problem(line, std::string("bad ") + key + " - banker disabled");
    };

    int line_no = 0;
    for (const std::string& raw : split_lines(text ? text : "")) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0)
            continue;
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        bool* seen = key == "hours" ? &saw_hours : key == "fee" ? &saw_fee
            : key == "maxdays"                                  ? &saw_maxdays
            : key == "racial_markup"                            ? &saw_markup
                                                                : nullptr;
        if (!seen || eq == std::string::npos) {
            problem(line_no, "unknown setting - line ignored");
            continue;
        }
        if (*seen) {
            problem(line_no, "duplicate " + key + " - line ignored");
            continue;
        }
        *seen = true;
        int number = 0;
        if (key == "hours") {
            if (!vendor_hours_parse(value, &config.hours)) {
                config.hours.clear();
                strict(line_no, "hours");
            }
        } else if (key == "fee") {
            if (!parse_number(value, &number) || number < 1 || number > BANKER_FEE_MAX)
                strict(line_no, "fee");
            else
                config.fee = number;
        } else if (key == "maxdays") {
            if (!parse_number(value, &number) || number < 1 || number > BANKER_MAXDAYS_MAX)
                strict(line_no, "maxdays");
            else
                config.maxdays = number;
        } else {
            if (value == "yes")
                config.markup = BANKER_MARKUP_DEFAULT;
            else if (!parse_number(value, &number) || number < 1 || number > BANKER_MARKUP_MAX)
                strict(line_no, "racial_markup");
            else
                config.markup = number;
        }
    }
    if (saw_fee && config.fee > 0 && !saw_maxdays) {
        config.ok = false;
        problem(0, "fee without maxdays - banker disabled");
    }
    return config;
}

int bank_side_for_race(int race)
{
    if (race >= RACE_HUMAN && race <= RACE_BEORNING)
        return BANK_SIDE_LIGHT;
    if (race == RACE_URUK || race == RACE_ORC || race == RACE_OLOGHAI)
        return BANK_SIDE_DARK;
    if (race == RACE_MAGUS || race == RACE_HARADRIM)
        return BANK_SIDE_THIRD;
    return BANK_SIDE_NONE;
}

const char* bank_side_file_name(int side)
{
    switch (side) {
    case BANK_SIDE_LIGHT:
        return "vault_light.json";
    case BANK_SIDE_DARK:
        return "vault_dark.json";
    case BANK_SIDE_THIRD:
        return "vault_third.json";
    default:
        return nullptr;
    }
}

int bank_days_stored(time_t deposited, time_t now, int start_hour)
{
    long days = bank_day_index(now, start_hour) - bank_day_index(deposited, start_hour);
    return days < 0 ? 0 : (int)days;
}

long long bank_fee(const banker_config& config, int days, int items, bool other_race)
{
    if (config.fee <= 0 || days <= 0 || items <= 0)
        return 0;
    long long fee = (long long)std::min(days, config.maxdays) * config.fee * items;
    if (other_race && config.markup > 0)
        fee = (fee * (100 + config.markup) + 99) / 100;
    return fee;
}
