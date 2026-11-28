#pragma once

#include "../game_time_text.h"
#include "../structs.h"

extern struct time_info_data time_info;
extern struct weather_data weather_info;

// Saves the game clock and weather on construction and restores both on destruction, along with
// the shared time text built from them, so a test can set the hour, date or moon without leaking
// them into later tests.
class ScopedGameClock {
public:
    ScopedGameClock()
        : m_time_info(time_info)
        , m_weather_info(weather_info)
    {
    }

    ~ScopedGameClock()
    {
        time_info = m_time_info;
        weather_info = m_weather_info;
        game_time_text::refresh();
    }

private:
    // The game time in force before the test changed it.
    time_info_data m_time_info;
    // The weather in force before the test changed it; the moon fields live here.
    weather_data m_weather_info;
};
