/* world_clock.cpp */

#include "world_clock.h"

//============================================================================
WorldClock::time_point WorldClock::now()
{
    // The monotonic clock keeps world time from jumping when the system time is changed.
    const std::chrono::steady_clock::time_point real_now = std::chrono::steady_clock::now();
    const std::chrono::steady_clock::duration real_time_since_epoch = real_now.time_since_epoch();
    const duration time_since_epoch = std::chrono::duration_cast<duration>(real_time_since_epoch);
    return time_point(time_since_epoch);
}
