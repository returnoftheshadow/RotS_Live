/* game_tick_schedule_contract.h */

#ifndef GAME_TICK_SCHEDULE_CONTRACT_H
#define GAME_TICK_SCHEDULE_CONTRACT_H

#include "../game_tick_schedule.h"
#include "../structs.h"

#include <gtest/gtest.h>

// Checks one pass against the rules every GameTickSchedule keeps: the phase is in range, the
// hourly update comes with the fast update at phase 0, and the phase advances by one,
// wrapping to 0, exactly on a pass that runs the fast update. previous_phase is the phase in
// effect before this pass: the previous pass's, or the phase the schedule starts in for its
// first pass.
inline testing::AssertionResult keeps_the_phase_contract(int previous_phase,
    const GameTickSchedule::WorldPass& world_pass)
{
    if (world_pass.time_phase < 0 || world_pass.time_phase >= FAST_UPDATE_RATE) {
        return testing::AssertionFailure() << "phase " << world_pass.time_phase
                                           << " is outside 0 to " << FAST_UPDATE_RATE - 1;
    }

    if (world_pass.run_hourly_update && !world_pass.run_fast_update) {
        return testing::AssertionFailure() << "the hourly update runs without the fast update";
    }

    if (world_pass.run_hourly_update && world_pass.time_phase != 0) {
        return testing::AssertionFailure()
            << "the hourly update runs at phase " << world_pass.time_phase << ", not 0";
    }

    int expected_phase = previous_phase;
    if (world_pass.run_fast_update) {
        expected_phase = (previous_phase + 1) % FAST_UPDATE_RATE;
    }

    if (world_pass.time_phase != expected_phase) {
        return testing::AssertionFailure()
            << "the phase is " << world_pass.time_phase << " where " << expected_phase
            << " follows phase " << previous_phase;
    }

    return testing::AssertionSuccess();
}

#endif /* GAME_TICK_SCHEDULE_CONTRACT_H */
