/* harness_control_channel.h */

#ifndef HARNESS_CONTROL_CHANNEL_H
#define HARNESS_CONTROL_CHANNEL_H

#include <string>
#include <string_view>
#include <vector>

namespace harness {

// Carries request lines from the integration harness to the game thread, and replies back.
class HarnessControlChannel {
public:
    virtual ~HarnessControlChannel() = default;

    // Removes and returns the lines received since the last call, oldest first, without their
    // newlines.
    [[nodiscard]] virtual std::vector<std::string> take_lines() = 0;

    // Sends text to the harness as one reply line.
    virtual void write_reply(std::string_view text) = 0;

    // True once the harness has closed its side; no line arrives after those already received.
    [[nodiscard]] virtual bool input_closed() const = 0;
};

} // namespace harness

#endif /* HARNESS_CONTROL_CHANNEL_H */
