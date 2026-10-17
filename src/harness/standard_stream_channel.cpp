/* standard_stream_channel.cpp */

#include "standard_stream_channel.h"

#include "utils.h"

namespace harness {

//============================================================================
StandardStreamChannel::StandardStreamChannel(std::istream& input, std::ostream& output)
    : reply_output(output)
    , line_reader(input)
{
}

//============================================================================
std::vector<std::string> StandardStreamChannel::take_lines()
{
    return line_reader.take_lines();
}

//============================================================================
void StandardStreamChannel::write_reply(std::string_view text)
{
    if (output_failed) {
        return;
    }

    reply_output << REPLY_PREFIX << text << '\n' << std::flush;
    if (!reply_output) {
        output_failed = true;
        const std::string message = std::string("SYSERR: ") + __func__
            + ": a harness reply could not be written; no further replies will be sent.";
        log(message.c_str());
    }
}

//============================================================================
bool StandardStreamChannel::input_closed() const
{
    return line_reader.input_closed();
}

} // namespace harness
