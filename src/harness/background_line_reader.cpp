/* background_line_reader.cpp */

#include "background_line_reader.h"

#include <functional>
#include <utility>

namespace harness {

//============================================================================
BackgroundLineReader::BackgroundLineReader(std::istream& input)
    : received_input(std::make_shared<ReceivedInput>())
    , reading_thread(&BackgroundLineReader::read_lines, std::ref(input), received_input)
{
}

//============================================================================
BackgroundLineReader::~BackgroundLineReader()
{
    if (!reading_thread.joinable()) {
        return;
    }

    if (input_closed()) {
        reading_thread.join();
        return;
    }

    // The thread holds its own share of the received input, so it stays safe once detached.
    reading_thread.detach();
}

//============================================================================
void BackgroundLineReader::read_lines(
    std::istream& input, std::shared_ptr<ReceivedInput> received)
{
    // The read is unbounded: the only writer is the test program that started the server, so a
    // fixed buffer would guard against nothing realistic. A line too long to be a request is
    // refused after it is read.
    std::string line;
    while (std::getline(input, line)) {
        std::scoped_lock lock(received->guard);
        // getline empties line before its next read, so the moved-from string is reused safely.
        received->lines.push_back(std::move(line));
    }

    std::scoped_lock lock(received->guard);
    received->closed = true;
}

//============================================================================
std::vector<std::string> BackgroundLineReader::take_lines()
{
    std::scoped_lock lock(received_input->guard);
    std::vector<std::string> taken = std::exchange(received_input->lines, {});
    return taken;
}

//============================================================================
bool BackgroundLineReader::input_closed() const
{
    std::scoped_lock lock(received_input->guard);
    return received_input->closed;
}

} // namespace harness
