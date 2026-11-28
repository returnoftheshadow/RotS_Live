/* affect_hours_registration.cpp */

#include "affect_hours_registration.h"

#include "advance_hours_request.h"
#include "affect_hours_schedule.h"
#include "harness_request.h"
#include "schedule_definition.h"
#include "schedule_setup.h"

#include <memory>
#include <string>
#include <utility>

namespace harness {

namespace {
// The schedule's line in the harness binary's usage text.
constexpr std::string_view AFFECT_HOURS_SUMMARY
    = "whole game hours of the hourly and fast updates, run only as advance-hours grants them";

// Builds an AffectHoursSchedule with an AdvanceHoursRequest that grants it hours. Returns a
// setup with no schedule if the request cannot be registered; the registry logs why.
[[nodiscard]] ScheduleSetup create_affect_hours_setup()
{
    std::shared_ptr<AffectHoursSchedule> hours_schedule = std::make_shared<AffectHoursSchedule>();
    std::unique_ptr<HarnessRequest> advance_hours
        = std::make_unique<AdvanceHoursRequest>(hours_schedule);
    ScheduleSetup setup;
    const bool request_registered = setup.requests.register_request(std::move(advance_hours));
    if (!request_registered) {
        return ScheduleSetup {};
    }

    setup.ready_state = "hours=" + std::to_string(hours_schedule->hours_completed());
    setup.schedule = std::move(hours_schedule);
    return setup;
}
} // namespace

//============================================================================
bool register_affect_hours_schedule(ScheduleRegistry& registry)
{
    ScheduleDefinition definition;
    definition.name = std::string(AFFECT_HOURS_SCHEDULE_NAME);
    definition.summary = std::string(AFFECT_HOURS_SUMMARY);
    definition.create_setup = &create_affect_hours_setup;
    return registry.register_schedule(std::move(definition));
}

} // namespace harness
