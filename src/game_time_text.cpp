#include "game_time_text.h"

#include "structs.h"
#include "utils.h"

#include <cstdlib>
#include <string>
#include <string_view>

extern struct time_info_data time_info;
extern struct weather_data weather_info;
extern int sun_events[12][2];
extern char* weekdays[];
extern char* moon_phase[];

namespace game_time_text {

namespace {

// The hour as hour_text() returns it; rebuilt by refresh().
std::string cached_hour_text;
// The `time` command report as time_report() returns it; rebuilt by refresh().
std::string cached_time_report;

std::string build_hour_text(int hours)
{
    int clock_hour = hours % 12;
    if (clock_hour == 0) {
        clock_hour = 12;
    }
    std::string_view meridiem = "AM";
    if (hours >= 12) {
        meridiem = "PM";
    }

    std::string text = "It is about ";
    text += std::to_string(clock_hour);
    text += ":00 ";
    text += meridiem;
    return text;
}

void append_hour_count(std::string& out_report, int hours)
{
    out_report += std::to_string(hours);
    out_report += " hour";
    if (hours != 1) {
        out_report += "s";
    }
}

// The line endings mix "\r\n" and "\n\r" because that is what the `time` command has always
// sent; clients and triggers may match on the exact text.
std::string build_time_report(std::string_view hour)
{
    std::string report(hour);

    // A month has 30 days and a week has 7.
    const int weekday = ((30 * time_info.month) + time_info.day + 1) % 7;
    report += " on ";
    report += weekdays[weekday];
    report += ", ";

    // day_to_str() writes "the <ordinal> day of <month name>", well under this size.
    char day_text[128];
    day_to_str(&time_info, day_text);
    report += day_text;
    report += ".\r\n";

    char* year = nth(time_info.year);
    report += "By the Steward's Reckoning, it is the ";
    report += year;
    report += " year of the fourth age of Arda.\r\n";
    free(year);

    report += "The moon is ";
    report += moon_phase[weather_info.moonphase];
    if (weather_info.moonlight) {
        report += " and shining.\n\r";
    } else {
        report += " and not shining.\n\r";
    }

    const int sunrise = sun_events[time_info.month][0];
    const int sunset = sun_events[time_info.month][1];
    if (time_info.hours >= sunrise && time_info.hours < sunset) {
        report += "The sun will set in about ";
        append_hour_count(report, sunset - time_info.hours);
        report += ".\r\n";
    } else {
        int hours_until_sunrise = sunrise - time_info.hours;
        if (time_info.hours >= 12) {
            hours_until_sunrise += 24;
        }
        report += "The sun will rise in about ";
        append_hour_count(report, hours_until_sunrise);
        report += ".\n\r";
    }

    return report;
}

} // namespace

void refresh()
{
    cached_hour_text = build_hour_text(time_info.hours);
    cached_time_report = build_time_report(cached_hour_text);
}

const std::string& hour_text()
{
    return cached_hour_text;
}

const std::string& time_report()
{
    return cached_time_report;
}

} // namespace game_time_text
