/* lap_timer.h */

#ifndef LAP_TIMER_H
#define LAP_TIMER_H

#include <chrono>

// Measures the real time that passes between successive readings, on a monotonic clock that
// changes to the system time do not affect.
class LapTimer {
public:
    // Starts timing from the moment of construction.
    LapTimer();

    // Returns the seconds since the previous call, or since construction on the first call.
    // Never negative.
    float get_elapsed_seconds();

private:
    // When the last reading was taken: construction, then each get_elapsed_seconds() call.
    std::chrono::steady_clock::time_point last_reading;
};

#endif /* LAP_TIMER_H */
