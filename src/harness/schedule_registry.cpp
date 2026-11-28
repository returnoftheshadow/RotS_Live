/* schedule_registry.cpp */

#include "schedule_registry.h"

#include "utils.h"

#include <string>
#include <utility>

namespace harness {

//============================================================================
bool ScheduleRegistry::register_schedule(ScheduleDefinition definition)
{
    if (!definition.create_setup) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the schedule '"
            + definition.name + "' has no factory and cannot be registered.";
        log(message.c_str());
        return false;
    }

    const ScheduleDefinition* const registered = find(definition.name);
    if (registered != nullptr) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the schedule '"
            + definition.name + "' is already registered.";
        log(message.c_str());
        return false;
    }

    definitions.push_back(std::move(definition));
    return true;
}

//============================================================================
const ScheduleDefinition* ScheduleRegistry::find(std::string_view name) const
{
    for (const ScheduleDefinition& definition : definitions) {
        if (definition.name == name) {
            return &definition;
        }
    }
    return nullptr;
}

//============================================================================
std::vector<std::string> ScheduleRegistry::names() const
{
    std::vector<std::string> registered_names;
    registered_names.reserve(definitions.size());
    for (const ScheduleDefinition& definition : definitions) {
        registered_names.push_back(definition.name);
    }
    return registered_names;
}

} // namespace harness
