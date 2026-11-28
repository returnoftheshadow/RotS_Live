/* real_time_tick_schedule.cpp */

#include "real_time_tick_schedule.h"

#include "structs.h"

namespace {
// Game-loop passes in one game hour.
constexpr int PULSES_PER_GAME_HOUR = SECS_PER_MUD_HOUR * TICS_PER_SECOND;
// Keeps zone updates off the passes that run the fast update.
constexpr int ZONE_UPDATE_OFFSET = 3;
// Keeps mobile activity off the passes that run the fast update or a zone update.
constexpr int MOBILE_ACTIVITY_OFFSET = 9;
// Passes between skill-timer countdowns: one second.
constexpr int SKILL_TIMER_INTERVAL = TICS_PER_SECOND;

// Each cadence must repeat within the game hour for pulse_of_hour alone to decide it.
static_assert(PULSES_PER_GAME_HOUR % PULSE_ZONE == 0, "Zone updates repeat within the game hour.");
static_assert(PULSES_PER_GAME_HOUR % PULSE_MOBILE == 0,
    "Mobile activity repeats within the game hour.");
static_assert(PULSES_PER_GAME_HOUR % PULSE_FAST_UPDATE == 0,
    "Fast updates repeat within the game hour.");
static_assert(PULSES_PER_GAME_HOUR % SKILL_TIMER_INTERVAL == 0,
    "Skill-timer countdowns repeat within the game hour.");
static_assert(PULSES_PER_GAME_HOUR / PULSE_FAST_UPDATE == FAST_UPDATE_RATE,
    "The game hour holds FAST_UPDATE_RATE fast updates, one per phase.");
static_assert(PULSES_PER_GAME_HOUR == 240,
    "real_time_tick_schedule.h documents 240 passes per game hour.");
} // namespace

//============================================================================
GameTickSchedule::WorldPass RealTimeTickSchedule::next_pass()
{
    pulse_of_hour = (pulse_of_hour + 1) % PULSES_PER_GAME_HOUR;

    WorldPass world_pass;
    world_pass.time_phase = pulse_of_hour / PULSE_FAST_UPDATE;
    world_pass.run_zone_update = (pulse_of_hour + ZONE_UPDATE_OFFSET) % PULSE_ZONE == 0;
    world_pass.run_mobile_activity = (pulse_of_hour + MOBILE_ACTIVITY_OFFSET) % PULSE_MOBILE == 0;
    world_pass.run_violence = true;
    world_pass.run_hourly_update = pulse_of_hour == 0;
    world_pass.run_fast_update = pulse_of_hour % PULSE_FAST_UPDATE == 0;
    world_pass.run_skill_timer = pulse_of_hour % SKILL_TIMER_INTERVAL == 0;
    return world_pass;
}
