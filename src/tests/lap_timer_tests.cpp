#include "../lap_timer.h"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace {
// Long enough that a reading after it stays distinguishable from an immediate one, even when an
// emulated or loaded machine stalls the test thread.
constexpr std::chrono::milliseconds PAUSE_LENGTH(250);
constexpr float PAUSE_SECONDS = std::chrono::duration<float>(PAUSE_LENGTH).count();
// Room for a stalled or emulated machine; a reading in the wrong unit is off by a factor of 1000.
constexpr float MARGIN_SECONDS = 5.0f;
} // namespace

TEST(LapTimer, FirstReadingMeasuresFromConstruction)
{
    LapTimer timer;
    std::this_thread::sleep_for(PAUSE_LENGTH);

    const float elapsed_seconds = timer.get_elapsed_seconds();

    EXPECT_GE(elapsed_seconds, PAUSE_SECONDS)
        << "Expected the first reading to include the pause since the timer was constructed.";
    EXPECT_LT(elapsed_seconds, PAUSE_SECONDS + MARGIN_SECONDS)
        << "Expected the first reading in seconds, close to the pause.";
}

TEST(LapTimer, EachReadingMeasuresFromThePreviousReading)
{
    LapTimer timer;
    std::this_thread::sleep_for(PAUSE_LENGTH);
    const float first_reading = timer.get_elapsed_seconds();

    const float second_reading = timer.get_elapsed_seconds();

    ASSERT_GE(first_reading, PAUSE_SECONDS) << "Expected the first reading to include the pause.";
    EXPECT_GE(second_reading, 0.0f) << "Expected a reading never to be negative.";
    EXPECT_LT(second_reading, PAUSE_SECONDS / 2)
        << "Expected an immediate second reading to measure from the first, not to include the "
           "pause before it.";
}
