#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <chrono>

// Minimal monotonic stopwatch built on std::chrono::steady_clock. Portable standard C++
// with no POSIX/Linux timing dependency. Call start() then stop() around the region to
// measure; elapsed<Duration>() returns the span in whatever std::chrono duration the
// caller asks for (e.g. elapsed<std::chrono::milliseconds>()), defaulting to microseconds.
class Stopwatch {
    // Timestamp captured by start() -- the beginning of the measured interval.
    std::chrono::steady_clock::time_point start_;
    // Timestamp captured by stop() -- the end of the measured interval.
    std::chrono::steady_clock::time_point stop_;

public:
    void start() {
        start_ = std::chrono::steady_clock::now();
    }

    void stop() {
        stop_ = std::chrono::steady_clock::now();
    }

    template <typename DurationT = std::chrono::microseconds>
    DurationT elapsed() const {
        return std::chrono::duration_cast<DurationT>(stop_ - start_);
    }
};

#endif // STOPWATCH_H
