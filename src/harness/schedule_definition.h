/* schedule_definition.h */

#ifndef SCHEDULE_DEFINITION_H
#define SCHEDULE_DEFINITION_H

#include "schedule_setup.h"

#include <functional>
#include <string>

namespace harness {

// One schedule a harness run may choose: its name, its usage summary and its factory.
struct ScheduleDefinition {
    // The name the --schedule option gives the schedule, and the ready reply reports.
    std::string name;
    // A one-line description for the harness binary's usage text.
    std::string summary;
    // Builds a fresh setup for one run. It returns a setup with no schedule when it cannot build
    // one.
    std::function<ScheduleSetup()> create_setup;
};

} // namespace harness

#endif /* SCHEDULE_DEFINITION_H */
