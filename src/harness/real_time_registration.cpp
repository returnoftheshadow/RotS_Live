/* real_time_registration.cpp */

#include "real_time_registration.h"

#include "real_time_tick_schedule.h"
#include "schedule_definition.h"
#include "schedule_setup.h"
#include "structs.h"

#include <memory>
#include <string>
#include <utility>

namespace harness {

namespace {
// The schedule's line in the harness binary's usage text.
constexpr std::string_view REAL_TIME_SUMMARY
    = "the live server's cadence, one game hour every 240 passes; accepts no requests";
static_assert(SECS_PER_MUD_HOUR * TICS_PER_SECOND == 240, "REAL_TIME_SUMMARY says a game hour is 240 passes.");

// Builds a RealTimeTickSchedule with no requests and an empty ready state.
[[nodiscard]] ScheduleSetup create_real_time_setup()
{
    ScheduleSetup setup;
    setup.schedule = std::make_shared<RealTimeTickSchedule>();
    return setup;
}
} // namespace

//============================================================================
bool register_real_time_schedule(ScheduleRegistry& registry)
{
    ScheduleDefinition definition;
    definition.name = std::string(REAL_TIME_SCHEDULE_NAME);
    definition.summary = std::string(REAL_TIME_SUMMARY);
    definition.create_setup = &create_real_time_setup;
    return registry.register_schedule(std::move(definition));
}

} // namespace harness
