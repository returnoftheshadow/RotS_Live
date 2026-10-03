#include "game_time_text.h"

#include "number_text.h"
#include "structs.h"

#include <cstddef>
#include <string>
#include <string_view>

extern struct time_info_data time_info;
extern struct weather_data weather_info;
extern int sun_events[12][2];
extern char* weekdays[];
extern char* month_name[];
extern char* moon_phase[];

namespace game_time_text {

namespace {

// The hour as hour_text() returns it; rebuilt by refresh().
std::string cached_hour_text;
// The `time` command report as time_report() returns it; rebuilt by refresh().
std::string cached_time_report;

// The longest hour text, "It is about 12:00 PM".
constexpr std::size_t hour_text_capacity = 20;
// The longest report, with the longest weekday, month name and moon phase and a five-digit year,
// is 242 characters.
constexpr std::size_t time_report_capacity = 256;

void build_hour_text(int hours, std::string& out_text)
{
    int clock_hour = hours % 12;
    if (clock_hour == 0) {
        clock_hour = 12;
    }
    std::string_view meridiem = "AM";
    if (hours >= 12) {
        meridiem = "PM";
    }

    out_text.clear();
    out_text.reserve(hour_text_capacity);
    out_text += "It is about ";
    append_number(out_text, clock_hour);
    out_text += ":00 ";
    out_text += meridiem;
}

void append_hour_count(std::string& out_report, int hours)
{
    append_number(out_report, hours);
    out_report += " hour";
    if (hours != 1) {
        out_report += "s";
    }
}

// The line endings mix "\r\n" and "\n\r" because that is what the `time` command has always
// sent; clients and triggers may match on the exact text.
void build_time_report(std::string_view hour, std::string& out_report)
{
    out_report.clear();
    out_report.reserve(time_report_capacity);
    out_report += hour;

    // A month has 30 days and a week has 7.
    const int weekday = ((30 * time_info.month) + time_info.day + 1) % 7;
    out_report += " on ";
    out_report += weekdays[weekday];
    out_report += ", ";

    out_report += "the ";
    append_ordinal(out_report, time_info.day + 1);
    out_report += " day of ";
    out_report += month_name[time_info.month];
    out_report += ".\r\n";

    out_report += "By the Steward's Reckoning, it is the ";
    append_ordinal(out_report, time_info.year);
    out_report += " year of the fourth age of Arda.\r\n";

    out_report += "The moon is ";
    out_report += moon_phase[weather_info.moonphase];
    if (weather_info.moonlight) {
        out_report += " and shining.\n\r";
    } else {
        out_report += " and not shining.\n\r";
    }

    const int sunrise = sun_events[time_info.month][0];
    const int sunset = sun_events[time_info.month][1];
    if (time_info.hours >= sunrise && time_info.hours < sunset) {
        out_report += "The sun will set in about ";
        append_hour_count(out_report, sunset - time_info.hours);
        out_report += ".\r\n";
    } else {
        int hours_until_sunrise = sunrise - time_info.hours;
        if (time_info.hours >= 12) {
            hours_until_sunrise += 24;
        }
        out_report += "The sun will rise in about ";
        append_hour_count(out_report, hours_until_sunrise);
        out_report += ".\n\r";
    }
}

} // namespace

void refresh()
{
    // Building in place keeps each string's buffer, so after the first hour a refresh allocates
    // nothing.
    build_hour_text(time_info.hours, cached_hour_text);
    build_time_report(cached_hour_text, cached_time_report);
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
