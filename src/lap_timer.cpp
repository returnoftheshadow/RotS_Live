/* lap_timer.cpp */

#include "lap_timer.h"

//============================================================================
LapTimer::LapTimer()
    : last_reading(std::chrono::steady_clock::now())
{
}

//============================================================================
float LapTimer::get_elapsed_seconds()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> elapsed = now - last_reading;
    last_reading = now;
    return elapsed.count();
}
