/* request_dispatch.h */

#ifndef REQUEST_DISPATCH_H
#define REQUEST_DISPATCH_H

#include "harness_request_registry.h"
#include "request_outcome.h"

#include <cstddef>
#include <string_view>

namespace harness {

// The longest request line accepted, in characters, not counting its line end.
inline constexpr std::size_t MAX_REQUEST_LINE_LENGTH = 64;

// Answers one line received from the integration harness, after ignoring one trailing carriage
// return. A line naming a registered request, by the text before its first space, is answered
// by that request's handle(). Every other line is answered now with "error empty request",
// "error request longer than <MAX_REQUEST_LINE_LENGTH> characters", or
// "error <line>: unknown request".
[[nodiscard]] RequestOutcome dispatch_request_line(
    std::string_view line, HarnessRequestRegistry& requests);

} // namespace harness

#endif /* REQUEST_DISPATCH_H */
