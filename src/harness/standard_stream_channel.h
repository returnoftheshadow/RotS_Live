/* standard_stream_channel.h */

#ifndef STANDARD_STREAM_CHANNEL_H
#define STANDARD_STREAM_CHANNEL_H

#include "background_line_reader.h"
#include "harness_control_channel.h"

#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace harness {

// The text that starts every reply line, so the harness can tell replies from other output.
inline constexpr std::string_view REPLY_PREFIX = "harness: ";

// The control channel over a pair of streams, normally the process's standard input and
// output. A BackgroundLineReader reads input on a thread of its own; every member function runs
// on the caller's thread.
class StandardStreamChannel : public HarnessControlChannel {
public:
    // Starts reading input on the reader's thread. output must outlive the channel. Nothing else
    // may use input until it ends or fails, and it must stay valid until then, even after the
    // channel is destroyed.
    StandardStreamChannel(std::istream& input, std::ostream& output);

    // Destroys the reader, which joins its thread if input has ended and detaches it otherwise.
    // A normal SIGTERM stop ends in exit(0) inside hupsig without destroying the channel, so
    // this matters for tests and for any path that returns from run_the_game.
    ~StandardStreamChannel() override = default;

    // A copy would share the reader.
    StandardStreamChannel(const StandardStreamChannel&) = delete;
    StandardStreamChannel& operator=(const StandardStreamChannel&) = delete;

    // See HarnessControlChannel. A line arrives whole, however long it is.
    [[nodiscard]] std::vector<std::string> take_lines() override;

    // Writes REPLY_PREFIX, text and a newline, then flushes. Once output has failed, logs that
    // once and writes nothing more.
    void write_reply(std::string_view text) override;

    // See HarnessControlChannel.
    [[nodiscard]] bool input_closed() const override;

private:
    // Where replies go; borrowed.
    std::ostream& reply_output;
    // True once a reply could not be written; later replies are dropped.
    bool output_failed = false;
    // Reads the request lines from input.
    BackgroundLineReader line_reader;
};

} // namespace harness

#endif /* STANDARD_STREAM_CHANNEL_H */
