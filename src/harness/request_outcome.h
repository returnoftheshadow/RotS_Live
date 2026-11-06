/* request_outcome.h */

#ifndef REQUEST_OUTCOME_H
#define REQUEST_OUTCOME_H

#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace harness {

// A reply body to send as soon as its request has been handled.
struct ImmediateReply {
    // The whole reply body, starting with its "done " or "error " word.
    std::string body;
};

// A reply body to send once a number of game-loop passes have been returned.
struct DeferredReply {
    // How many passes, counting the one on which the request is handled, must be returned before
    // the reply is sent; at least 1.
    std::uint64_t pass_count = 1;
    // Builds the whole reply body when it is sent. It refers to state it does not own, such as
    // the request's schedule, through a std::weak_ptr, never a reference.
    std::function<std::string()> build_body;
};

// How a request is answered: now, or after its passes have run.
using RequestOutcome = std::variant<ImmediateReply, DeferredReply>;

} // namespace harness

#endif /* REQUEST_OUTCOME_H */
