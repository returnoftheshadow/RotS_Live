/* harness_request_registry.h */

#ifndef HARNESS_REQUEST_REGISTRY_H
#define HARNESS_REQUEST_REGISTRY_H

#include "harness_request.h"

#include <memory>
#include <string_view>
#include <vector>

namespace harness {

// Owns the requests one schedule accepts, each under its own name.
class HarnessRequestRegistry {
public:
    HarnessRequestRegistry() = default;

    // Owns its requests, so it cannot be copied. Deleted explicitly because a vector of unique_ptr
    // otherwise reports itself copyable.
    HarnessRequestRegistry(const HarnessRequestRegistry&) = delete;
    HarnessRequestRegistry& operator=(const HarnessRequestRegistry&) = delete;
    HarnessRequestRegistry(HarnessRequestRegistry&&) noexcept = default;
    HarnessRequestRegistry& operator=(HarnessRequestRegistry&&) noexcept = default;

    // Takes ownership of request and files it under its name(). Returns true when it is
    // registered. A null request, or one whose name is already registered, is logged and
    // destroyed, the registry is left unchanged, and false is returned.
    [[nodiscard]] bool register_request(std::unique_ptr<HarnessRequest> request);

    // Returns the request registered under name, or null when there is none. The request lives
    // as long as the registry.
    [[nodiscard]] HarnessRequest* find(std::string_view name);

private:
    // The registered requests, never null. A schedule accepts a handful, so a linear search
    // costs less than a map's allocations.
    std::vector<std::unique_ptr<HarnessRequest>> requests;
};

} // namespace harness

#endif /* HARNESS_REQUEST_REGISTRY_H */
