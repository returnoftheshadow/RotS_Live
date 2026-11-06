/* fake_control_channel.h */

#ifndef FAKE_CONTROL_CHANNEL_H
#define FAKE_CONTROL_CHANNEL_H

#include "harness_control_channel.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace harness_tests {

// A control channel whose lines, replies and closed input a test controls through shared state,
// which stays readable after the channel has been moved into the code under test.
class FakeControlChannel : public harness::HarnessControlChannel {
public:
    // What the channel shares with its test.
    struct State {
        // Lines the next take_lines() returns.
        std::vector<std::string> lines_to_deliver;
        // Every reply written, in order.
        std::vector<std::string> replies;
        // What input_closed() reports.
        bool closed = false;
    };

    // Reads and writes state, which must not be null.
    explicit FakeControlChannel(std::shared_ptr<State> state)
        : shared_state(std::move(state))
    {
    }

    [[nodiscard]] std::vector<std::string> take_lines() override
    {
        std::vector<std::string> taken;
        taken.swap(shared_state->lines_to_deliver);
        return taken;
    }

    void write_reply(std::string_view text) override
    {
        shared_state->replies.emplace_back(text);
    }

    [[nodiscard]] bool input_closed() const override
    {
        return shared_state->closed;
    }

private:
    // The state the test feeds and inspects.
    std::shared_ptr<State> shared_state;
};

} // namespace harness_tests

#endif /* FAKE_CONTROL_CHANNEL_H */
