/* real_time_tick_schedule.h */

#ifndef REAL_TIME_TICK_SCHEDULE_H
#define REAL_TIME_TICK_SCHEDULE_H

#include "game_tick_schedule.h"

// Schedules the periodic world updates at the game's normal cadence, one game hour every
// 240 passes.
class RealTimeTickSchedule : public GameTickSchedule {
public:
    [[nodiscard]] WorldPass next_pass() override;

private:
    // The current pass's position in the game hour, 0 to 239. Position 0 is the pass on
    // which the hour advances.
    int pulse_of_hour = 0;
};

#endif /* REAL_TIME_TICK_SCHEDULE_H */
