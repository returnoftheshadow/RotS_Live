/* schedule_registry.h */

#ifndef SCHEDULE_REGISTRY_H
#define SCHEDULE_REGISTRY_H

#include "schedule_definition.h"

#include <string>
#include <string_view>
#include <vector>

namespace harness {

// Holds the schedules a harness run may choose, by name, in the order they were registered.
class ScheduleRegistry {
public:
    // Adds definition. Returns true when it is registered. A definition with no factory, or
    // whose name is already registered, is logged, the registry is left unchanged, and false is
    // returned.
    [[nodiscard]] bool register_schedule(ScheduleDefinition definition);

    // Returns the definition registered under name, or null when there is none. The pointer
    // stays valid until the next registration or the registry's destruction.
    [[nodiscard]] const ScheduleDefinition* find(std::string_view name) const;

    // The registered names, in the order they were registered.
    [[nodiscard]] std::vector<std::string> names() const;

private:
    // The registered definitions, oldest first. A harness has a handful, so a linear search
    // costs less than a map and keeps the registration order.
    std::vector<ScheduleDefinition> definitions;
};

} // namespace harness

#endif /* SCHEDULE_REGISTRY_H */
