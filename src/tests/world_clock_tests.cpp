#include "../world_clock.h"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <type_traits>

// The standard library's Clock requirements, checked at compile time.
static_assert(std::is_same<WorldClock::time_point::clock, WorldClock>::value,
    "WorldClock's instants belong to WorldClock.");
static_assert(std::is_same<WorldClock::time_point::duration, WorldClock::duration>::value,
    "WorldClock's instants are measured in its own duration.");
static_assert(std::is_same<WorldClock::rep, WorldClock::duration::rep>::value,
    "WorldClock's rep is its duration's rep.");
static_assert(std::is_same<WorldClock::period, WorldClock::duration::period>::value,
    "WorldClock's period is its duration's period.");
static_assert(std::is_same<decltype(WorldClock::now()), WorldClock::time_point>::value,
    "WorldClock::now() returns a WorldClock instant.");
static_assert(WorldClock::is_steady, "WorldClock never runs backwards.");

namespace {
// Long enough to measure reliably, even under emulation.
constexpr std::chrono::milliseconds PAUSE_LENGTH(100);
// Room for a stalled or emulated machine; a clock running at the wrong rate is off by far more.
constexpr std::chrono::seconds MARGIN(5);
} // namespace

TEST(WorldClock, NeverRunsBackwards)
{
    const WorldClock::time_point earlier = WorldClock::now();
    const WorldClock::time_point later = WorldClock::now();

    const WorldClock::duration difference = later - earlier;
    EXPECT_GE(later, earlier) << "Expected a later reading to be no earlier; the difference was "
                              << difference.count() << " ns.";
}

TEST(WorldClock, AdvancesAtTheRateOfRealTime)
{
    const WorldClock::time_point before_pause = WorldClock::now();
    std::this_thread::sleep_for(PAUSE_LENGTH);
    const WorldClock::time_point after_pause = WorldClock::now();

    const WorldClock::duration world_elapsed = after_pause - before_pause;
    EXPECT_GE(world_elapsed, PAUSE_LENGTH)
        << "Expected world time to advance by at least the real pause.";
    EXPECT_LT(world_elapsed, PAUSE_LENGTH + MARGIN)
        << "Expected world time to advance by about the real pause; it advanced "
        << world_elapsed.count() << " ns.";
}
