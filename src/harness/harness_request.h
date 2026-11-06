/* harness_request.h */

#ifndef HARNESS_REQUEST_H
#define HARNESS_REQUEST_H

#include "request_outcome.h"

#include <string_view>

namespace harness {

// One request the integration harness may send, owning its name, its grammar and its reply
// texts.
class HarnessRequest {
public:
    virtual ~HarnessRequest() = default;

    // The first word of a line that asks for this request. The text stays valid for the
    // request's lifetime.
    [[nodiscard]] virtual std::string_view name() const = 0;

    // Answers one request line. line is the whole line without its line end; arguments is the
    // text after the first space, empty when the line has none. Returns whole reply bodies, their
    // "done " or "error " word included.
    [[nodiscard]] virtual RequestOutcome handle(std::string_view line, std::string_view arguments)
        = 0;
};

} // namespace harness

#endif /* HARNESS_REQUEST_H */
