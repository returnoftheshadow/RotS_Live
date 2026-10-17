#include "../game_time_text.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"

#include "ScopedGameClock.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

ACMD(do_time);
void clear_char(struct char_data* ch, int mode);
void reset_time(void);

namespace {

// A player whose descriptor buffers what the `time` command prints.
class TimeCommandReader {
public:
    TimeCommandReader()
    {
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;

        clear_char(&m_character, MOB_VOID);
        m_character.player.name = strdup("Timekeeper");
        m_character.player.level = 10;
        m_character.desc = &m_descriptor;
        m_descriptor.character = &m_character;
    }

    ~TimeCommandReader()
    {
        free(m_character.player.name);
    }

    std::string read_time()
    {
        char empty_argument[] = "";
        do_time(&m_character, empty_argument, nullptr, 0, 0);
        return std::string(m_descriptor.small_outbuf);
    }

private:
    // Buffers the command output; never connected to a socket.
    descriptor_data m_descriptor {};
    // The player typing `time`.
    char_data m_character {};
};

// Night in the third month (sunrise 7, sunset 19) under a full, shining moon.
void set_night_of_third_month()
{
    time_info.hours = 23;
    time_info.day = 4;
    time_info.month = 2;
    time_info.moon = 0;
    time_info.year = 1234;
    weather_info.moonphase = MOON_FULL;
    weather_info.moonlight = 1;
}

} // namespace

TEST(TimeCommand, ReportsHourWeekdayDateYearMoonAndSunrise)
{
    ScopedGameClock clock_scope;
    set_night_of_third_month();
    game_time_text::refresh();
    TimeCommandReader reader;

    EXPECT_EQ(reader.read_time(),
        "It is about 11:00 PM on Isilya, the 5th day of S\xFAlim\xEB.\r\n"
        "By the Steward's Reckoning, it is the 1234th year of the fourth age of Arda.\r\n"
        "The moon is full and shining.\n\r"
        "The sun will rise in about 8 hours.\n\r");
}

TEST(TimeCommand, ReportsSunsetInOneHourBeforeDusk)
{
    ScopedGameClock clock_scope;
    set_night_of_third_month();
    time_info.hours = 18;
    weather_info.moonphase = MOON_NEW;
    weather_info.moonlight = 0;
    game_time_text::refresh();
    TimeCommandReader reader;

    EXPECT_EQ(reader.read_time(),
        "It is about 6:00 PM on Isilya, the 5th day of S\xFAlim\xEB.\r\n"
        "By the Steward's Reckoning, it is the 1234th year of the fourth age of Arda.\r\n"
        "The moon is new and not shining.\n\r"
        "The sun will set in about 1 hour.\r\n");
}

TEST(TimeCommand, ReportsSunriseInOneHourBeforeDawn)
{
    ScopedGameClock clock_scope;
    set_night_of_third_month();
    time_info.hours = 6;
    game_time_text::refresh();
    TimeCommandReader reader;

    EXPECT_EQ(reader.read_time(),
        "It is about 6:00 AM on Isilya, the 5th day of S\xFAlim\xEB.\r\n"
        "By the Steward's Reckoning, it is the 1234th year of the fourth age of Arda.\r\n"
        "The moon is full and shining.\n\r"
        "The sun will rise in about 1 hour.\n\r");
}

TEST(GameTimeText, HourTextStopsBeforeTheWeekday)
{
    ScopedGameClock clock_scope;
    set_night_of_third_month();

    game_time_text::refresh();

    EXPECT_EQ(game_time_text::hour_text(), "It is about 11:00 PM");
}

TEST(GameTimeText, HourTextShowsMidnightAndNoonAsTwelve)
{
    ScopedGameClock clock_scope;
    set_night_of_third_month();

    time_info.hours = 0;
    game_time_text::refresh();
    EXPECT_EQ(game_time_text::hour_text(), "It is about 12:00 AM");

    time_info.hours = 12;
    game_time_text::refresh();
    EXPECT_EQ(game_time_text::hour_text(), "It is about 12:00 PM");
}

TEST(GameTimeText, TimeReportStaysUntilTheNextRefresh)
{
    ScopedGameClock clock_scope;
    set_night_of_third_month();
    game_time_text::refresh();

    time_info.hours = 3;

    EXPECT_EQ(game_time_text::hour_text(), "It is about 11:00 PM");
    EXPECT_EQ(game_time_text::time_report().rfind("It is about 11:00 PM on ", 0), 0u);
}

TEST(GameTimeText, ResetTimeRefreshesTheText)
{
    ScopedGameClock clock_scope;
    set_night_of_third_month();
    // Decades of real time have passed since the game began, so the real year is never 1.
    time_info.year = 1;
    game_time_text::refresh();

    reset_time();

    int clock_hour = time_info.hours % 12;
    if (clock_hour == 0) {
        clock_hour = 12;
    }
    std::string meridiem = "AM";
    if (time_info.hours >= 12) {
        meridiem = "PM";
    }
    EXPECT_EQ(game_time_text::hour_text(),
        "It is about " + std::to_string(clock_hour) + ":00 " + meridiem);
    const std::string year_text = " " + std::to_string(time_info.year);
    EXPECT_NE(game_time_text::time_report().find(year_text), std::string::npos);
}
