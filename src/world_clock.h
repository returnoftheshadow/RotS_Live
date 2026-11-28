/* world_clock.h */

#ifndef WORLD_CLOCK_H
#define WORLD_CLOCK_H

#include <chrono>

// The game world's timeline, which world rules use to measure how long ago something happened.
// It meets the standard library's Clock requirements, so its instants and spans are ordinary
// std::chrono values. It currently advances at the rate of real time.
struct WorldClock {
    // Spans of world time, and their representation, in nanoseconds.
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    // An instant on the world timeline. Only differences between instants are meaningful.
    using time_point = std::chrono::time_point<WorldClock>;

    // World time never runs backwards between calls to now().
    static constexpr bool is_steady = true;

    // Returns the current instant of world time.
    static time_point now();
};

#endif /* WORLD_CLOCK_H */
