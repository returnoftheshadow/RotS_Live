#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <chrono>

// Minimal monotonic stopwatch on std::chrono::steady_clock. Portable, no POSIX timing.
// Call start() then stop() around the region; elapsed<Duration>() returns the span in the
// requested std::chrono duration, defaulting to microseconds.
class Stopwatch {
    // Timestamp captured by start() -- the beginning of the measured interval.
    std::chrono::steady_clock::time_point start_;
    // Timestamp captured by stop() -- the end of the measured interval.
    std::chrono::steady_clock::time_point stop_;

public:
    void start() { start_ = std::chrono::steady_clock::now(); }
    void stop() { stop_ = std::chrono::steady_clock::now(); }

    template <typename DurationT = std::chrono::microseconds>
    DurationT elapsed() const {
        return std::chrono::duration_cast<DurationT>(stop_ - start_);
    }
};

#endif // STOPWATCH_H
