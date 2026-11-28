/* affect_hours_schedule.h */

#ifndef AFFECT_HOURS_SCHEDULE_H
#define AFFECT_HOURS_SCHEDULE_H

#include "game_tick_schedule.h"

#include <optional>
#include <string>

namespace harness {

// Schedules whole game hours of the hourly and fast updates, and only the hours granted to it.
// It never runs zone updates, mobile activity, combat or skill-timer countdowns.
class AffectHoursSchedule : public GameTickSchedule {
public:
    // The most game hours that may be granted and not yet run.
    static constexpr int MAX_PENDING_HOURS = 1000;

    // Adds hours game hours after any already granted. Returns an empty value when they are
    // granted. Otherwise returns why they are refused, leaving the schedule unchanged: hours is
    // below 1, or the hours pending, counting one under way, would exceed MAX_PENDING_HOURS.
    [[nodiscard]] std::optional<std::string> grant_hours(int hours);

    // Returns the next granted pass, or a pass with no world update when nothing is granted. A
    // granted hour is fast updates at phases 1 to FAST_UPDATE_RATE - 1, then the hourly update
    // with the fast update at phase 0. Every pass carries the phase in force, which is 0
    // between granted hours.
    [[nodiscard]] WorldPass next_pass() override;

    // The granted passes next_pass() has not yet returned.
    [[nodiscard]] int pending_passes() const;

    // The granted hours whose last pass next_pass() has returned.
    [[nodiscard]] int hours_completed() const;

private:
    // Granted passes still to be returned.
    int passes_pending = 0;
    // The phase of the last fast update returned; 0 before the first.
    int current_phase = 0;
    // Hours whose last pass has been returned.
    int completed_hours = 0;
};

} // namespace harness

#endif /* AFFECT_HOURS_SCHEDULE_H */
