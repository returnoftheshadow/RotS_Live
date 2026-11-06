/* affect_hours_schedule.cpp */

#include "affect_hours_schedule.h"

#include "structs.h"

#include <limits>
#include <string>

namespace harness {

namespace {
static_assert(AffectHoursSchedule::MAX_PENDING_HOURS
        <= std::numeric_limits<int>::max() / FAST_UPDATE_RATE,
    "The most passes that can be pending fit in an int.");
} // namespace

//============================================================================
std::optional<std::string> AffectHoursSchedule::grant_hours(int hours)
{
    if (hours < 1) {
        return std::string("hours must be at least 1");
    }

    // An hour under way still counts as pending, so round the pending passes up to hours.
    const int pending_hours = (passes_pending + FAST_UPDATE_RATE - 1) / FAST_UPDATE_RATE;
    if (hours > MAX_PENDING_HOURS - pending_hours) {
        return "at most " + std::to_string(MAX_PENDING_HOURS) + " hours may be pending";
    }

    passes_pending += hours * FAST_UPDATE_RATE;
    return std::nullopt;
}

//============================================================================
GameTickSchedule::WorldPass AffectHoursSchedule::next_pass()
{
    WorldPass world_pass;
    if (passes_pending == 0) {
        world_pass.time_phase = current_phase;
        return world_pass;
    }

    passes_pending--;
    current_phase = (current_phase + 1) % FAST_UPDATE_RATE;
    world_pass.time_phase = current_phase;
    world_pass.run_fast_update = true;
    if (current_phase == 0) {
        world_pass.run_hourly_update = true;
        completed_hours++;
    }
    return world_pass;
}

//============================================================================
int AffectHoursSchedule::pending_passes() const
{
    return passes_pending;
}

//============================================================================
int AffectHoursSchedule::hours_completed() const
{
    return completed_hours;
}

} // namespace harness
