/* reader_test_input.h */

#ifndef READER_TEST_INPUT_H
#define READER_TEST_INPUT_H

#include <chrono>
#include <istream>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>

namespace harness_tests {

// A test's input stream, kept valid for as long as a reading thread might use it. Declare it
// before the reader or channel that reads it.
class ReaderTestInput {
public:
    // How long wait_until_closed waits for the reading thread to reach the end of input.
    static constexpr std::chrono::seconds WAIT_LIMIT { 5 };

    // An input that reads text.
    explicit ReaderTestInput(const std::string& text)
        : ReaderTestInput(std::make_unique<std::stringbuf>(text, std::ios::in))
    {
    }

    // An input that reads from buffer, which must not be null.
    explicit ReaderTestInput(std::unique_ptr<std::streambuf> buffer)
        : owned_buffer(std::move(buffer))
        , owned_stream(std::make_unique<std::istream>(owned_buffer.get()))
        , input_stream(*owned_stream)
    {
    }

    // The stream to hand to the reader; valid for the input's lifetime, or forever once leaked.
    [[nodiscard]] std::istream& stream()
    {
        return input_stream;
    }

    // Waits until source reports its input closed and returns true. After WAIT_LIMIT it leaks
    // the stream and its buffer and returns false, because the reader then detaches a thread
    // that may still be reading them.
    template <typename InputSource>
    [[nodiscard]] bool wait_until_closed(const InputSource& source)
    {
        const std::chrono::steady_clock::time_point deadline
            = std::chrono::steady_clock::now() + WAIT_LIMIT;
        while (!source.input_closed()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                // Deliberate leak: freeing them could free them under the detached thread.
                static_cast<void>(owned_stream.release());
                static_cast<void>(owned_buffer.release());
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

private:
    // The buffer the stream reads; declared first so it outlives the stream.
    std::unique_ptr<std::streambuf> owned_buffer;
    // The stream over owned_buffer, until a timed-out wait leaks it.
    std::unique_ptr<std::istream> owned_stream;
    // The stream, still reachable after owned_stream has released it.
    std::istream& input_stream;
};

} // namespace harness_tests

#endif /* READER_TEST_INPUT_H */
