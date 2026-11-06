/* request_dispatch.cpp */

#include "request_dispatch.h"

#include "harness_request.h"

#include <string>
#include <utility>

namespace harness {

//============================================================================
RequestOutcome dispatch_request_line(std::string_view line, HarnessRequestRegistry& requests)
{
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }

    if (line.size() > MAX_REQUEST_LINE_LENGTH) {
        std::string refusal = "error request longer than "
            + std::to_string(MAX_REQUEST_LINE_LENGTH) + " characters";
        return ImmediateReply { std::move(refusal) };
    }

    if (line.empty()) {
        return ImmediateReply { "error empty request" };
    }

    const std::size_t space_position = line.find(' ');
    const std::string_view request_name = line.substr(0, space_position);
    std::string_view arguments;
    if (space_position != std::string_view::npos) {
        arguments = line.substr(space_position + 1);
    }

    HarnessRequest* const request = requests.find(request_name);
    if (request == nullptr) {
        std::string refusal = "error " + std::string(line) + ": unknown request";
        return ImmediateReply { std::move(refusal) };
    }
    return request->handle(line, arguments);
}

} // namespace harness
