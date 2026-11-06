/* affect_hours_registration.h */

#ifndef AFFECT_HOURS_REGISTRATION_H
#define AFFECT_HOURS_REGISTRATION_H

#include "schedule_registry.h"

#include <string_view>

namespace harness {

// The name the affect-hours schedule is registered under.
inline constexpr std::string_view AFFECT_HOURS_SCHEDULE_NAME = "affect-hours";

// Registers the affect-hours schedule under AFFECT_HOURS_SCHEDULE_NAME. Each setup its factory
// builds holds a fresh AffectHoursSchedule, which nothing advances until an AdvanceHoursRequest
// grants it hours, and the ready state "hours=0". Returns false, with the refusal logged, when
// the registry refuses it.
[[nodiscard]] bool register_affect_hours_schedule(ScheduleRegistry& registry);

} // namespace harness

#endif /* AFFECT_HOURS_REGISTRATION_H */
