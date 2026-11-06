/* fake_harness_request.h */

#ifndef FAKE_HARNESS_REQUEST_H
#define FAKE_HARNESS_REQUEST_H

#include "harness_request.h"
#include "request_outcome.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace harness_tests {

// A request that answers with the outcomes its test scripts, and records what it was handed.
class FakeHarnessRequest : public harness::HarnessRequest {
public:
    // What the request shares with its test.
    struct State {
        // The outcomes handle() returns, oldest first. With none left, handle() replies at once
        // with FakeHarnessRequest::UNSCRIPTED_REPLY.
        std::deque<harness::RequestOutcome> outcomes;
        // The line of every handle() call, in order.
        std::vector<std::string> handled_lines;
        // The arguments of every handle() call, in order.
        std::vector<std::string> handled_arguments;
        // Run at the start of every handle() call when set.
        std::function<void()> on_handle;
    };

    // The reply to a call with no outcome scripted.
    static constexpr std::string_view UNSCRIPTED_REPLY = "done unscripted";

    // Answers lines named request_name, recording into state, which must not be null.
    FakeHarnessRequest(std::string request_name, std::shared_ptr<State> state)
        : registered_name(std::move(request_name))
        , shared_state(std::move(state))
    {
    }

    [[nodiscard]] std::string_view name() const override
    {
        return registered_name;
    }

    [[nodiscard]] harness::RequestOutcome handle(
        std::string_view line, std::string_view arguments) override
    {
        if (shared_state->on_handle) {
            shared_state->on_handle();
        }
        shared_state->handled_lines.emplace_back(line);
        shared_state->handled_arguments.emplace_back(arguments);
        if (shared_state->outcomes.empty()) {
            return harness::ImmediateReply { std::string(UNSCRIPTED_REPLY) };
        }

        harness::RequestOutcome outcome = std::move(shared_state->outcomes.front());
        shared_state->outcomes.pop_front();
        return outcome;
    }

private:
    // The name name() reports.
    std::string registered_name;
    // The script the test writes and the record it reads.
    std::shared_ptr<State> shared_state;
};

} // namespace harness_tests

#endif /* FAKE_HARNESS_REQUEST_H */
