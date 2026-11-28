/* real_time_registration.h */

#ifndef REAL_TIME_REGISTRATION_H
#define REAL_TIME_REGISTRATION_H

#include "schedule_registry.h"

#include <string_view>

namespace harness {

// The name the real-time schedule is registered under.
inline constexpr std::string_view REAL_TIME_SCHEDULE_NAME = "real-time";

// Registers the real-time schedule under REAL_TIME_SCHEDULE_NAME. Each setup its factory builds
// holds a fresh RealTimeTickSchedule, which advances the world at the live server's cadence, no
// requests and an empty ready state. Returns false, with the refusal logged, when the registry
// refuses it.
[[nodiscard]] bool register_real_time_schedule(ScheduleRegistry& registry);

} // namespace harness

#endif /* REAL_TIME_REGISTRATION_H */
