/* harness_request_registry.cpp */

#include "harness_request_registry.h"

#include "utils.h"

#include <string>
#include <utility>

namespace harness {

//============================================================================
bool HarnessRequestRegistry::register_request(std::unique_ptr<HarnessRequest> request)
{
    if (request == nullptr) {
        const std::string message
            = std::string("SYSERR: ") + __func__ + ": a null request cannot be registered.";
        log(message.c_str());
        return false;
    }

    const std::string_view request_name = request->name();
    const HarnessRequest* const registered = find(request_name);
    if (registered != nullptr) {
        const std::string message = std::string("SYSERR: ") + __func__ + ": the request '"
            + std::string(request_name) + "' is already registered.";
        log(message.c_str());
        return false;
    }

    requests.push_back(std::move(request));
    return true;
}

//============================================================================
HarnessRequest* HarnessRequestRegistry::find(std::string_view name)
{
    for (const std::unique_ptr<HarnessRequest>& request : requests) {
        if (request->name() == name) {
            return request.get();
        }
    }
    return nullptr;
}

} // namespace harness
